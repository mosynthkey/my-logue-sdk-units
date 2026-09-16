#pragma once

/*
 * File: dubthrow.h
 *
 * Dub-desk send throw delay for NTS-3. Dry always passes; pad throw feeds a
 * BPM-synced stereo delay. Feedback path: mild soft sat → bandpass (Y = tone),
 * then loop AGC so high FDBK sustains without parking on the rails.
 * Release stops new send while the loop decays. Not a dry-kill echo out.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class DubThrow : public Processor
{
public:
  // Dotted 1/4 @ 40 BPM ≈ 108k; headroom for spread + interpolation.
  static constexpr uint32_t kMaxDelaySamples = 120000U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kSpreadMax = 0.06f;
  // Mild tape-ish drive only — large pre-gain + BP makeup was railing the loop.
  static constexpr float kDriveGain = 1.06f;
  static constexpr float kFilterMakeup = 1.12f;
  static constexpr float kFeedbackMax = 0.97f;
  static constexpr float kLoopCeiling = 0.82f;
  static constexpr float kSendDuckStart = 0.55f;
  static constexpr float kSendDuckFloor = 0.32f;

  uint32_t getBufferSize() const override final
  {
    return kMaxDelaySamples * 2U;
  }

  enum
  {
    THROW = 0U,
    TONE,
    DEPTH,
    TIME,
    FDBK,
    SPRD,
    MODE,
    TOUCH,
    NUM_PARAMS
  };

  enum
  {
    TIME_16 = 0,
    TIME_8,
    TIME_8D,
    TIME_4,
    TIME_4D,
    NUM_TIMES
  };

  enum
  {
    MODE_PPONG = 0,
    MODE_DUAL,
    MODE_MONO,
    NUM_MODES
  };

  enum
  {
    TOUCH_GATE = 0,
    TOUCH_LATCH,
    TOUCH_ALWAYS,
    NUM_TOUCH
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case THROW:
      throw_norm_ = param_10bit_to_f32(value);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      tone_dirty_ = true;
      break;
    case DEPTH:
      depth_ = fx::clip01(value / 1000.f);
      break;
    case TIME:
    {
      int32_t time_sel = value;
      if (time_sel < 0)
        time_sel = 0;
      if (time_sel >= NUM_TIMES)
        time_sel = NUM_TIMES - 1;
      time_sel_ = static_cast<uint8_t>(time_sel);
      break;
    }
    case FDBK:
      feedback_norm_ = param_10bit_to_f32(value);
      break;
    case SPRD:
      spread_norm_ = param_10bit_to_f32(value);
      break;
    case MODE:
    {
      int32_t mode = value;
      if (mode < 0)
        mode = 0;
      if (mode >= NUM_MODES)
        mode = NUM_MODES - 1;
      mode_ = static_cast<uint8_t>(mode);
      break;
    }
    case TOUCH:
    {
      int32_t touch = value;
      if (touch < 0)
        touch = 0;
      if (touch >= NUM_TOUCH)
        touch = NUM_TOUCH - 1;
      const uint8_t next = static_cast<uint8_t>(touch);
      if (next == TOUCH_GATE && !pad_held_)
        latched_ = false;
      if (next == TOUCH_ALWAYS)
        latched_ = false;
      touch_mode_ = next;
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *time_names[NUM_TIMES] = {"1/16", "1/8", "1/8D", "1/4", "1/4D"};
    static const char *mode_names[NUM_MODES] = {"PPONG", "DUAL", "MONO"};
    static const char *touch_names[NUM_TOUCH] = {"GATE", "LATCH", "ALWAYS"};

    if (index == TIME && value >= 0 && value < NUM_TIMES)
      return time_names[value];
    if (index == MODE && value >= 0 && value < NUM_MODES)
      return mode_names[value];
    if (index == TOUCH && value >= 0 && value < NUM_TOUCH)
      return touch_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    delay_left_ = allocated_buffer;
    delay_right_ = allocated_buffer + kMaxDelaySamples;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    throw_norm_ = 0.f;
    tone_norm_ = 0.55f;
    depth_ = 1.f;
    feedback_norm_ = 0.55f;
    spread_norm_ = 0.35f;
    time_sel_ = TIME_8D;
    mode_ = MODE_PPONG;
    touch_mode_ = TOUCH_GATE;
    bpm_ = 120.f;
    write_pos_ = 0U;
    pad_held_ = false;
    latched_ = false;
    send_smooth_ = 0.f;
    throw_smooth_ = 0.f;
    tone_dirty_ = true;
    refreshTone();
    resetFilterState();
    lim_env_ = 0.f;
    loop_env_ = 0.f;
  }

  void teardown() override final
  {
    delay_left_ = nullptr;
    delay_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    pad_held_ = false;
    latched_ = false;
    send_smooth_ = 0.f;
    throw_smooth_ = 0.f;
    lim_env_ = 0.f;
    loop_env_ = 0.f;
    resetFilterState();
    if (delay_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxDelaySamples; ++sampleIndex)
      {
        delay_left_[sampleIndex] = 0.f;
        delay_right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    pad_held_ = true;
    if (touch_mode_ == TOUCH_LATCH)
      latched_ = true;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out,
               uint32_t frames)
  {
    if (delay_left_ == nullptr || delay_right_ == nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
      {
        out[0] = in[0];
        out[1] = in[1];
        in += 2;
        out += 2;
      }
      return;
    }

    if (tone_dirty_)
      refreshTone();

    const float delay_base = noteDelaySamples();
    const float spread = spread_norm_ * kSpreadMax;
    float delay_l = delay_base * (1.f - spread * 0.5f);
    float delay_r = delay_base * (1.f + spread * 0.5f);
    if (delay_l < 64.f)
      delay_l = 64.f;
    if (delay_r < 64.f)
      delay_r = 64.f;
    const float delay_max = static_cast<float>(kMaxDelaySamples - 4U);
    if (delay_l > delay_max)
      delay_l = delay_max;
    if (delay_r > delay_max)
      delay_r = delay_max;

    const float feedback = feedback_norm_ * kFeedbackMax;
    const float send_target = sendOpen() ? 1.f : 0.f;
    // ~1 ms smooth for send gate / throw (near-1 linearization, not fasterexpf).
    const float smooth_coeff = 1.f / 48.f;
    const float lim_attack = 1.f / 16.f;
    const float lim_release = 1.f / 480.f;
    // Loop AGC: fast grab, slower release so high FDBK can bloom without rail-lock.
    const float loop_attack = 1.f / 32.f;
    const float loop_release = 1.f / 960.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      send_smooth_ += (send_target - send_smooth_) * smooth_coeff;
      throw_smooth_ += (throw_norm_ - throw_smooth_) * smooth_coeff;

      const float send_amount = throw_smooth_ * depth_ * send_smooth_;
      // Duck new throw into an already-hot tank (desk-style, avoids instant squash).
      float send_duck = 1.f;
      if (loop_env_ > kSendDuckStart)
      {
        const float duck_amount =
            fx::clip01((loop_env_ - kSendDuckStart) / (kLoopCeiling - kSendDuckStart + 1e-6f));
        send_duck = fx::mix(1.f, kSendDuckFloor, duck_amount);
      }
      const float send_left = live_left * send_amount * send_duck;
      const float send_right = live_right * send_amount * send_duck;

      const float delayed_left = readDelay(delay_left_, write_pos_, delay_l);
      const float delayed_right = readDelay(delay_right_, write_pos_, delay_r);

      float fb_src_left = 0.f;
      float fb_src_right = 0.f;
      float wet_left = 0.f;
      float wet_right = 0.f;

      if (mode_ == MODE_PPONG)
      {
        fb_src_left = delayed_right;
        fb_src_right = delayed_left;
        wet_left = delayed_left;
        wet_right = delayed_right;
      }
      else if (mode_ == MODE_DUAL)
      {
        fb_src_left = delayed_left;
        fb_src_right = delayed_right;
        wet_left = delayed_left;
        wet_right = delayed_right;
      }
      else
      {
        const float delayed_mono = 0.5f * (delayed_left + delayed_right);
        fb_src_left = delayed_mono;
        fb_src_right = delayed_mono;
        wet_left = delayed_mono;
        wet_right = delayed_mono;
      }

      // Soft sat first, then tone filter — sat harmonics get filtered, BP peak is not pre-driven.
      const float sat_left = fx::softclip(fx::clip(fb_src_left * kDriveGain, -1.5f, 1.5f));
      const float sat_right = fx::softclip(fx::clip(fb_src_right * kDriveGain, -1.5f, 1.5f));
      const float filtered_left = processFeedbackFilter(sat_left, svf_low_l_, svf_band_l_);
      const float filtered_right = processFeedbackFilter(sat_right, svf_low_r_, svf_band_r_);

      float write_left = 0.f;
      float write_right = 0.f;
      if (mode_ == MODE_MONO)
      {
        const float mono_send = 0.5f * (send_left + send_right);
        const float mono_fb = 0.5f * (filtered_left + filtered_right) * feedback;
        write_left = mono_send + mono_fb;
        write_right = write_left;
      }
      else
      {
        write_left = send_left + filtered_left * feedback;
        write_right = send_right + filtered_right * feedback;
      }

      const float write_peak = fx::absf(write_left) > fx::absf(write_right) ? fx::absf(write_left)
                                                                             : fx::absf(write_right);
      const float loop_coeff = (write_peak > loop_env_) ? loop_attack : loop_release;
      loop_env_ += (write_peak - loop_env_) * loop_coeff;
      float loop_gain = 1.f;
      if (loop_env_ > kLoopCeiling)
        loop_gain = kLoopCeiling / loop_env_;
      write_left *= loop_gain;
      write_right *= loop_gain;

      // Safety only — AGC should keep most of the loop below hard softclip.
      write_left = fx::softclip(fx::clip(write_left, -1.5f, 1.5f));
      write_right = fx::softclip(fx::clip(write_right, -1.5f, 1.5f));

      delay_left_[write_pos_] = write_left;
      delay_right_[write_pos_] = write_right;
      ++write_pos_;
      if (write_pos_ >= kMaxDelaySamples)
        write_pos_ = 0U;

      // Wet ceiling via DEPTH; dry always passes.
      wet_left *= depth_;
      wet_right *= depth_;

      const float peak = fx::absf(wet_left) > fx::absf(wet_right) ? fx::absf(wet_left)
                                                                  : fx::absf(wet_right);
      const float lim_coeff = (peak > lim_env_) ? lim_attack : lim_release;
      lim_env_ += (peak - lim_env_) * lim_coeff;
      float lim_gain = 1.f;
      if (lim_env_ > 0.95f)
        lim_gain = 0.95f / lim_env_;
      wet_left = fx::softclip(fx::clip(wet_left * lim_gain, -1.5f, 1.5f));
      wet_right = fx::softclip(fx::clip(wet_right * lim_gain, -1.5f, 1.5f));

      out[0] = live_left + wet_left;
      out[1] = live_right + wet_right;

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  bool sendOpen() const
  {
    if (touch_mode_ == TOUCH_ALWAYS)
      return true;
    if (touch_mode_ == TOUCH_LATCH)
      return pad_held_ || latched_;
    return pad_held_;
  }

  float noteDelaySamples() const
  {
    static const float kBeats[NUM_TIMES] = {0.25f, 0.5f, 0.75f, 1.f, 1.5f};
    const float beats = kBeats[time_sel_ < NUM_TIMES ? time_sel_ : TIME_8D];
    return beats * 60.f / bpm_ * getSampleRate();
  }

  void refreshTone()
  {
    tone_dirty_ = false;
    // Dark (~180 Hz) → bright/nasal (~4.5 kHz).
    const float min_hz = 180.f;
    const float max_hz = 4500.f;
    const float hz = min_hz * fasterpowf(max_hz / min_hz, fx::clip01(tone_norm_));
    svf_f_ = 2.f * fastersinfullf(3.14159265f * fx::clip(hz, 80.f, 8000.f) / getSampleRate());
    // Mild resonance; a little more bite at higher tone.
    svf_damp_ = 0.35f + (1.f - tone_norm_) * 0.25f;
  }

  void resetFilterState()
  {
    svf_low_l_ = 0.f;
    svf_band_l_ = 0.f;
    svf_low_r_ = 0.f;
    svf_band_r_ = 0.f;
  }

  float processFeedbackFilter(float input, float &low, float &band)
  {
    low += svf_f_ * band;
    const float high = input - low - svf_damp_ * band;
    band += svf_f_ * high;
    // Bound SVF state so a hot loop cannot run away into denormals / huge BP peaks.
    low = fx::clip(low, -4.f, 4.f);
    band = fx::clip(band, -4.f, 4.f);
    // Blend BP with a touch of HP so lows damp in the loop (dub tape feel).
    // Modest makeup — former 1.8× made high FDBK rail within a few echoes.
    return (band * 0.85f + high * 0.15f) * kFilterMakeup;
  }

  static float readDelay(const float *buffer, uint32_t write_pos, float delay_samples)
  {
    float read_pos = static_cast<float>(write_pos) - delay_samples;
    while (read_pos < 0.f)
      read_pos += static_cast<float>(kMaxDelaySamples);
    const uint32_t index_a = static_cast<uint32_t>(read_pos);
    const float frac = read_pos - static_cast<float>(index_a);
    const uint32_t index_b = index_a + 1U >= kMaxDelaySamples ? 0U : index_a + 1U;
    return buffer[index_a] + (buffer[index_b] - buffer[index_a]) * frac;
  }

  float *delay_left_ = nullptr;
  float *delay_right_ = nullptr;
  uint32_t write_pos_ = 0U;

  float throw_norm_ = 0.f;
  float tone_norm_ = 0.55f;
  float depth_ = 1.f;
  float feedback_norm_ = 0.55f;
  float spread_norm_ = 0.35f;
  uint8_t time_sel_ = TIME_8D;
  uint8_t mode_ = MODE_PPONG;
  uint8_t touch_mode_ = TOUCH_GATE;
  float bpm_ = 120.f;

  bool pad_held_ = false;
  bool latched_ = false;
  float send_smooth_ = 0.f;
  float throw_smooth_ = 0.f;

  bool tone_dirty_ = true;
  float svf_f_ = 0.f;
  float svf_damp_ = 0.5f;
  float svf_low_l_ = 0.f;
  float svf_band_l_ = 0.f;
  float svf_low_r_ = 0.f;
  float svf_band_r_ = 0.f;
  float lim_env_ = 0.f;
  float loop_env_ = 0.f;
};
