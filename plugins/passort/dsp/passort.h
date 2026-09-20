#pragma once

/*
 * File: passort.h
 *
 * Performance Assort — NTS-3 XY pad that locks an effect from the touch
 * start region, then maps finger motion to that effect's parameters.
 *
 *   Top-left  → HPF   (right = cutoff, down = resonance)
 *   Top-right → LPF   (left  = cutoff, down = resonance)
 *   Center    → Tape stop
 *   Bottom-left  → Dotted-8th delay (up = depth, right = feedback HPF)
 *   Bottom-right → Beat-sync step roll (up = speed, left = shorter buffer)
 *
 * Prefer get_raw_input for capture (pad-up unit_render may be muted).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Passort : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 192000U;
  static constexpr uint32_t kMaxDelaySamples = 72000U;
  static constexpr uint32_t kMinSliceSamples = 64U;
  // Floor for audible step-roll loops (~5.3 ms @ 48 kHz). Shorter reads as a tone.
  static constexpr uint32_t kMinRollLoopSamples = 256U;
  static constexpr uint32_t kMinCaptureSamples = 1024U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kMinCapturePeak = 0.003f;
  static constexpr float kWetFadeSamples = 256.f;
  static constexpr float kCenterRadius = 190.f;
  static constexpr float kMinFilterCutoffHz = 40.f;
  static constexpr float kMaxFilterCutoffHz = 16000.f;
  static constexpr float kParamSmooth = 0.08f;

  uint32_t getBufferSize() const override final
  {
    return kMaxBufSamples * 2U + kMaxDelaySamples * 2U;
  }

  enum
  {
    MIX = 0U,
    TAPE,
    FDBK,
    GLUE,
    NUM_PARAMS
  };

  enum
  {
    MODE_NONE = 0,
    MODE_HPF,
    MODE_LPF,
    MODE_TAPE,
    MODE_DELAY,
    MODE_ROLL,
    NUM_MODES
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TAPE:
      tape_norm_ = param_10bit_to_f32(value);
      break;
    case FDBK:
      fdbk_norm_ = param_10bit_to_f32(value);
      break;
    case GLUE:
      glue_norm_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t, int32_t) const override final
  {
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_left_ = allocated_buffer;
    buf_right_ = allocated_buffer + kMaxBufSamples;
    delay_left_ = allocated_buffer + kMaxBufSamples * 2U;
    delay_right_ = allocated_buffer + kMaxBufSamples * 2U + kMaxDelaySamples;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    mix_ = 1.f;
    tape_norm_ = 0.35f;
    fdbk_norm_ = 0.55f;
    glue_norm_ = 0.25f;
    bpm_ = 120.f;
    updateLoopGeometry();
    reset();
  }

  void teardown() override final
  {
    buf_left_ = nullptr;
    buf_right_ = nullptr;
    delay_left_ = nullptr;
    delay_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    delay_pos_ = 0U;
    captured_samples_ = 0U;
    captured_peak_ = 0.f;
    pad_held_ = false;
    active_ = false;
    arming_ = false;
    mode_ = MODE_NONE;
    wet_ = 0.f;
    wet_target_ = 0.f;
    touch_x_ = 512.f;
    touch_y_ = 512.f;
    cutoff_smooth_ = 0.5f;
    res_smooth_ = 0.2f;
    depth_smooth_ = 0.f;
    fb_hpf_smooth_ = 0.3f;
    roll_speed_smooth_ = 0.4f;
    roll_len_smooth_ = 0.4f;
    play_pos_ = 0.f;
    tape_progress_ = 0.f;
    tape_rate_ = 1.f;
    loop_pos_ = 0.f;
    loop_start_ = 0U;
    loop_length_ = 2048U;
    frozen_origin_ = 0U;
    frozen_length_ = 0U;
    roll_retarget_counter_ = 0U;
    delay_hpf_left_.z = 0.f;
    delay_hpf_right_.z = 0.f;
    resetSvf();
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
    {
      bpm_ = tempo;
      updateLoopGeometry();
    }
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      requestRelease();
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    touch_x_ = static_cast<float>(x);
    touch_y_ = static_cast<float>(y);

    const bool new_touch = !pad_held_;
    pad_held_ = true;

    if (phase == k_unit_touch_phase_began || new_touch)
    {
      mode_ = modeFromStart(x, y);
      engageMode();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    updateTargetsFromTouch();

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      const bool freeze_playback =
          (mode_ == MODE_TAPE || mode_ == MODE_ROLL) && !arming_ && (active_ || wet_ > 0.f);
      if (!freeze_playback)
        recordSample(live_left, live_right);

      if (arming_)
      {
        if (captured_samples_ >= neededCaptureSamples() && captured_peak_ >= kMinCapturePeak)
        {
          arming_ = false;
          freezeWindow(neededCaptureSamples());
          startVoice();
          wet_target_ = 1.f;
        }
      }

      advanceWet();
      smoothParams();

      if (wet_ <= 0.001f || mode_ == MODE_NONE)
      {
        out[0] = live_left;
        out[1] = live_right;
        in += 2;
        if (raw != nullptr)
          raw += 2;
        out += 2;
        continue;
      }

      float fx_left = live_left;
      float fx_right = live_right;
      renderMode(live_left, live_right, fx_left, fx_right);

      const float wet_gain = wet_ * mix_;
      out[0] = fx::mix(live_left, fx_left, wet_gain);
      out[1] = fx::mix(live_right, fx_right, wet_gain);

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  uint8_t currentMode() const { return mode_; }
  bool isPadHeld() const { return pad_held_; }
  uint32_t rollLoopLength() const { return loop_length_; }
  uint32_t rollBufferLength() const { return frozen_length_; }

private:
  struct SvfState
  {
    float ic1 = 0.f;
    float ic2 = 0.f;
  };

  static float absf(float value) { return value < 0.f ? -value : value; }

  static uint8_t modeFromStart(uint32_t x, uint32_t y)
  {
    const float dx = static_cast<float>(x) - 511.5f;
    const float dy = static_cast<float>(y) - 511.5f;
    if (dx * dx + dy * dy <= kCenterRadius * kCenterRadius)
      return MODE_TAPE;

    const bool left = x < 512U;
    const bool top = y >= 512U;
    if (left && top)
      return MODE_HPF;
    if (!left && top)
      return MODE_LPF;
    if (left && !top)
      return MODE_DELAY;
    return MODE_ROLL;
  }

  void requestRelease()
  {
    wet_target_ = 0.f;
    arming_ = false;
    active_ = false;
  }

  void engageMode()
  {
    active_ = true;
    arming_ = false;
    resetSvf();
    delay_hpf_left_.z = 0.f;
    delay_hpf_right_.z = 0.f;

    if (mode_ == MODE_HPF || mode_ == MODE_LPF || mode_ == MODE_DELAY)
    {
      startVoice();
      wet_target_ = 1.f;
      return;
    }

    const uint32_t needed = neededCaptureSamples();
    if (captured_samples_ >= needed && captured_peak_ >= kMinCapturePeak)
    {
      freezeWindow(needed);
      startVoice();
      wet_target_ = 1.f;
      return;
    }

    arming_ = true;
    captured_peak_ = 0.f;
    wet_target_ = 0.f;
  }

  void startVoice()
  {
    play_pos_ = 0.f;
    tape_progress_ = 0.f;
    tape_rate_ = 1.f;
    loop_pos_ = 0.f;
    if (mode_ == MODE_ROLL)
      captureRoll();
  }

  uint32_t neededCaptureSamples() const
  {
    if (mode_ == MODE_TAPE)
    {
      const float stop_beats = 0.35f + tape_norm_ * 3.65f;
      uint32_t samples = static_cast<uint32_t>(stop_beats * 60.f / bpm_ * getSampleRate());
      if (samples < kMinCaptureSamples)
        samples = kMinCaptureSamples;
      if (samples > record_length_)
        samples = record_length_;
      return samples;
    }
    if (mode_ == MODE_ROLL)
    {
      const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
      uint32_t samples = static_cast<uint32_t>(beat * 2.f);
      if (samples < kMinCaptureSamples)
        samples = kMinCaptureSamples;
      if (samples > record_length_)
        samples = record_length_;
      return samples;
    }
    return kMinCaptureSamples;
  }

  void updateLoopGeometry()
  {
    const float seconds_per_bar = 240.f / bpm_;
    uint32_t samples = static_cast<uint32_t>(seconds_per_bar * getSampleRate() + 0.5f);
    if (samples < kMinCaptureSamples)
      samples = kMinCaptureSamples;
    if (samples > kMaxBufSamples)
      samples = kMaxBufSamples;
    record_length_ = samples;
  }

  void recordSample(float left, float right)
  {
    if (buf_left_ == nullptr)
      return;
    buf_left_[write_pos_] = left;
    buf_right_[write_pos_] = right;
    write_pos_ = (write_pos_ + 1U) % record_length_;
    if (captured_samples_ < record_length_)
      ++captured_samples_;
    const float peak = absf(left) + absf(right);
    if (peak > captured_peak_)
      captured_peak_ = peak;
  }

  void freezeWindow(uint32_t length)
  {
    if (length > captured_samples_)
      length = captured_samples_;
    if (length < kMinSliceSamples)
      length = kMinSliceSamples;
    if (length > record_length_)
      length = record_length_;

    frozen_length_ = length;
    const uint32_t newest_index = write_pos_ == 0U ? record_length_ - 1U : write_pos_ - 1U;
    int32_t origin = static_cast<int32_t>(newest_index) - static_cast<int32_t>(length) + 1;
    if (origin < 0)
      origin += static_cast<int32_t>(record_length_);
    frozen_origin_ = static_cast<uint32_t>(origin);
  }

  void advanceWet()
  {
    const float coeff = 1.f / kWetFadeSamples;
    if (wet_ < wet_target_)
    {
      wet_ += coeff;
      if (wet_ > wet_target_)
        wet_ = wet_target_;
    }
    else if (wet_ > wet_target_)
    {
      wet_ -= coeff;
      if (wet_ < wet_target_)
        wet_ = wet_target_;
    }
    if (wet_ <= 0.f && !pad_held_)
    {
      mode_ = MODE_NONE;
      resetSvf();
    }
  }

  void updateTargetsFromTouch()
  {
    const float x_norm = fx::clip01(touch_x_ * (1.f / 1023.f));
    const float y_norm = fx::clip01(touch_y_ * (1.f / 1023.f));
    const float from_top = 1.f - y_norm;

    cutoff_target_ = x_norm;
    // LPF: moving left closes the filter (lower cutoff).
    if (mode_ == MODE_LPF)
      cutoff_target_ = x_norm;
    res_target_ = from_top;
    depth_target_ = y_norm;
    fb_hpf_target_ = x_norm;
    // Bottom-right: up = faster grid, right = longer buffer (corner starts mild).
    roll_speed_target_ = y_norm;
    roll_len_target_ = x_norm;
  }

  void smoothParams()
  {
    cutoff_smooth_ += (cutoff_target_ - cutoff_smooth_) * kParamSmooth;
    res_smooth_ += (res_target_ - res_smooth_) * kParamSmooth;
    depth_smooth_ += (depth_target_ - depth_smooth_) * kParamSmooth;
    fb_hpf_smooth_ += (fb_hpf_target_ - fb_hpf_smooth_) * kParamSmooth;
    roll_speed_smooth_ += (roll_speed_target_ - roll_speed_smooth_) * kParamSmooth;
    roll_len_smooth_ += (roll_len_target_ - roll_len_smooth_) * kParamSmooth;
  }

  void resetSvf()
  {
    svf_left_ = SvfState();
    svf_right_ = SvfState();
  }

  static void tickSvf(float input, float g, float k, float drive_comp, SvfState &state, float &low,
                      float &band, float &high)
  {
    const float a1 = 1.f / (1.f + g * (g + k));
    const float a2 = g * a1;
    const float a3 = g * a2;
    const float driven = input * drive_comp;

    const float v3 = driven - state.ic2;
    const float v1 = a1 * state.ic1 + a2 * v3;
    const float v2 = state.ic2 + a2 * state.ic1 + a3 * v3;
    state.ic1 = fx::clip(2.f * v1 - state.ic1, -4.f, 4.f);
    state.ic2 = fx::clip(2.f * v2 - state.ic2, -4.f, 4.f);

    low = v2;
    band = v1;
    high = driven - k * v1 - v2;
  }

  float processFilter(float input, bool highpass, SvfState &state)
  {
    const float cutoff_hz =
        fx::clip(70.f * fasterpow2f(cutoff_smooth_ * 8.f), kMinFilterCutoffHz, kMaxFilterCutoffHz);
    const float g = fastertanfullf(3.14159265f * cutoff_hz / getSampleRate());
    const float q = 0.7f + res_smooth_ * res_smooth_ * 14.f;
    const float k = 1.f / q;
    const float drive_comp = 1.f / (1.f + res_smooth_ * res_smooth_ * 3.5f);

    float low = 0.f;
    float band = 0.f;
    float high = 0.f;
    tickSvf(input, g, k, drive_comp, state, low, band, high);
    return fx::softclip(highpass ? high : low);
  }

  void readFrozen(float pos, uint32_t window, float &left, float &right) const
  {
    if (buf_left_ == nullptr || window < 2U)
    {
      left = 0.f;
      right = 0.f;
      return;
    }
    float wrapped = pos;
    while (wrapped >= static_cast<float>(window))
      wrapped -= static_cast<float>(window);
    while (wrapped < 0.f)
      wrapped += static_cast<float>(window);

    const uint32_t index_a = static_cast<uint32_t>(wrapped);
    const uint32_t index_b = (index_a + 1U) % window;
    const float frac = wrapped - static_cast<float>(index_a);
    const uint32_t abs_a = (frozen_origin_ + index_a) % record_length_;
    const uint32_t abs_b = (frozen_origin_ + index_b) % record_length_;
    left = buf_left_[abs_a] + (buf_left_[abs_b] - buf_left_[abs_a]) * frac;
    right = buf_right_[abs_a] + (buf_right_[abs_b] - buf_right_[abs_a]) * frac;
  }

  void renderFilter(float live_left, float live_right, float &left, float &right, bool highpass)
  {
    left = processFilter(live_left, highpass, svf_left_);
    right = processFilter(live_right, highpass, svf_right_);
  }

  void renderTape(float &left, float &right)
  {
    const uint32_t window = frozen_length_ < kMinSliceSamples ? kMinSliceSamples : frozen_length_;
    const float stop_beats = 0.35f + tape_norm_ * 3.65f;
    const float stop_samples = stop_beats * 60.f / bpm_ * getSampleRate();
    tape_progress_ += 1.f / (stop_samples < 64.f ? 64.f : stop_samples);
    if (tape_progress_ > 1.f)
      tape_progress_ = 1.f;
    const float remain = 1.f - tape_progress_;
    tape_rate_ = remain * remain;
    if (tape_rate_ < 0.02f || play_pos_ >= static_cast<float>(window))
    {
      left = 0.f;
      right = 0.f;
      return;
    }
    readFrozen(play_pos_, window, left, right);
    play_pos_ += tape_rate_;
  }

  void renderDelay(float live_left, float live_right, float &left, float &right)
  {
    if (delay_left_ == nullptr)
    {
      left = live_left;
      right = live_right;
      return;
    }

    // Fixed dotted 8th: 0.75 beat.
    float delay_samples = 0.75f * 60.f / bpm_ * getSampleRate();
    if (delay_samples < 64.f)
      delay_samples = 64.f;
    if (delay_samples > static_cast<float>(kMaxDelaySamples - 4U))
      delay_samples = static_cast<float>(kMaxDelaySamples - 4U);

    const uint32_t delay_int = static_cast<uint32_t>(delay_samples);
    int32_t read_index = static_cast<int32_t>(delay_pos_) - static_cast<int32_t>(delay_int);
    if (read_index < 0)
      read_index += static_cast<int32_t>(kMaxDelaySamples);
    const uint32_t read_a = static_cast<uint32_t>(read_index);
    const float delayed_left = delay_left_[read_a];
    const float delayed_right = delay_right_[read_a];

    // Feedback HPF: right = higher cutoff on the feedback path.
    const float hpf_hz = 80.f + fb_hpf_smooth_ * fb_hpf_smooth_ * 6000.f;
    const float hpf_coeff = fx::onePoleCoeff(hpf_hz, getSampleRate());
    const float fb_left = delay_hpf_left_.processHp(delayed_left, hpf_coeff);
    const float fb_right = delay_hpf_right_.processHp(delayed_right, hpf_coeff);
    const float feedback = 0.12f + fdbk_norm_ * 0.72f;

    delay_left_[delay_pos_] = live_left + fb_left * feedback;
    delay_right_[delay_pos_] = live_right + fb_right * feedback;
    ++delay_pos_;
    if (delay_pos_ >= kMaxDelaySamples)
      delay_pos_ = 0U;

    // Up = depth (wet of delay against dry live).
    left = fx::mix(live_left, delayed_left, depth_smooth_);
    right = fx::mix(live_right, delayed_right, depth_smooth_);
  }

  static uint32_t rollDivisions(float speed_norm)
  {
    // Grid: 1/2 .. 1/32 of a beat (never sub-audible on its own).
    static const uint32_t kDivs[6] = {2U, 4U, 8U, 16U, 24U, 32U};
    const float select = fx::clip01(speed_norm) * 5.0001f;
    uint32_t step = static_cast<uint32_t>(select);
    if (step > 5U)
      step = 5U;
    return kDivs[step];
  }

  static float bufferBeats(float len_norm)
  {
    // Step-aligned capture windows: 1/4 .. 2 beats (shortest still musical).
    static const float kBeats[5] = {0.25f, 0.5f, 1.f, 1.5f, 2.f};
    const float select = fx::clip01(len_norm) * 4.0001f;
    uint32_t step = static_cast<uint32_t>(select);
    if (step > 4U)
      step = 4U;
    return kBeats[step];
  }

  void captureRoll()
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float buf_beats = bufferBeats(roll_len_smooth_);
    uint32_t buffer_length = static_cast<uint32_t>(buf_beats * beat);
    if (buffer_length < kMinRollLoopSamples)
      buffer_length = kMinRollLoopSamples;
    if (buffer_length > frozen_length_ && frozen_length_ >= kMinSliceSamples)
      buffer_length = frozen_length_;
    if (buffer_length > captured_samples_ && captured_samples_ >= kMinSliceSamples)
      buffer_length = captured_samples_;

    // Speed selects the musical grid; do not divide buffer by divisions
    // (that produced sub-ms loops / tonal buzz at the short+fast corner).
    const uint32_t divisions = rollDivisions(roll_speed_smooth_);
    uint32_t slice = static_cast<uint32_t>(beat / static_cast<float>(divisions));
    if (slice < kMinRollLoopSamples)
      slice = kMinRollLoopSamples;
    if (slice > buffer_length)
      slice = buffer_length;

    loop_length_ = slice;
    if (frozen_length_ >= slice)
    {
      loop_start_ = (frozen_origin_ + frozen_length_ - slice) % record_length_;
    }
    else
    {
      const uint32_t newest = write_pos_ == 0U ? record_length_ - 1U : write_pos_ - 1U;
      int32_t origin = static_cast<int32_t>(newest) - static_cast<int32_t>(slice) + 1;
      if (origin < 0)
        origin += static_cast<int32_t>(record_length_);
      loop_start_ = static_cast<uint32_t>(origin);
    }
    loop_pos_ = 0.f;
  }

  void renderRoll(float &left, float &right)
  {
    // Retarget slice length while held so speed / buffer gestures respond.
    ++roll_retarget_counter_;
    if ((roll_retarget_counter_ & 63U) == 0U)
      captureRoll();

    if (loop_length_ < 8U || buf_left_ == nullptr)
    {
      left = 0.f;
      right = 0.f;
      return;
    }

    const uint32_t index_a = (loop_start_ + static_cast<uint32_t>(loop_pos_)) % record_length_;
    const uint32_t index_b = (index_a + 1U) % record_length_;
    const float frac = loop_pos_ - static_cast<float>(static_cast<uint32_t>(loop_pos_));
    left = buf_left_[index_a] + (buf_left_[index_b] - buf_left_[index_a]) * frac;
    right = buf_right_[index_a] + (buf_right_[index_b] - buf_right_[index_a]) * frac;

    // Overlapping end→start join (not fade-to-silence — that buzzes on short loops).
    float xfade = 8.f + glue_norm_ * 96.f;
    const float max_xfade = static_cast<float>(loop_length_) * 0.25f;
    if (xfade > max_xfade)
      xfade = max_xfade;
    if (xfade > 1.f)
    {
      const float tail = static_cast<float>(loop_length_) - loop_pos_;
      if (tail < xfade)
      {
        const float fade = tail / xfade;
        const float secondary_pos = loop_pos_ + xfade - static_cast<float>(loop_length_);
        const uint32_t sec_a =
            (loop_start_ + static_cast<uint32_t>(secondary_pos)) % record_length_;
        const uint32_t sec_b = (sec_a + 1U) % record_length_;
        const float sec_frac =
            secondary_pos - static_cast<float>(static_cast<uint32_t>(secondary_pos));
        const float sec_left =
            buf_left_[sec_a] + (buf_left_[sec_b] - buf_left_[sec_a]) * sec_frac;
        const float sec_right =
            buf_right_[sec_a] + (buf_right_[sec_b] - buf_right_[sec_a]) * sec_frac;
        left = left * fade + sec_left * (1.f - fade);
        right = right * fade + sec_right * (1.f - fade);
      }
    }

    loop_pos_ += 1.f;
    if (loop_pos_ >= static_cast<float>(loop_length_))
      loop_pos_ -= static_cast<float>(loop_length_);
  }

  void renderMode(float live_left, float live_right, float &left, float &right)
  {
    switch (mode_)
    {
    case MODE_HPF:
      renderFilter(live_left, live_right, left, right, true);
      break;
    case MODE_LPF:
      renderFilter(live_left, live_right, left, right, false);
      break;
    case MODE_TAPE:
      renderTape(left, right);
      break;
    case MODE_DELAY:
      renderDelay(live_left, live_right, left, right);
      break;
    case MODE_ROLL:
      renderRoll(left, right);
      break;
    default:
      left = live_left;
      right = live_right;
      break;
    }
  }

  float *buf_left_ = nullptr;
  float *buf_right_ = nullptr;
  float *delay_left_ = nullptr;
  float *delay_right_ = nullptr;

  float mix_ = 1.f;
  float tape_norm_ = 0.35f;
  float fdbk_norm_ = 0.55f;
  float glue_norm_ = 0.25f;
  float bpm_ = 120.f;
  float wet_ = 0.f;
  float wet_target_ = 0.f;
  float touch_x_ = 512.f;
  float touch_y_ = 512.f;
  float cutoff_target_ = 0.5f;
  float cutoff_smooth_ = 0.5f;
  float res_target_ = 0.2f;
  float res_smooth_ = 0.2f;
  float depth_target_ = 0.f;
  float depth_smooth_ = 0.f;
  float fb_hpf_target_ = 0.3f;
  float fb_hpf_smooth_ = 0.3f;
  float roll_speed_target_ = 0.4f;
  float roll_speed_smooth_ = 0.4f;
  float roll_len_target_ = 0.4f;
  float roll_len_smooth_ = 0.4f;
  float play_pos_ = 0.f;
  float tape_progress_ = 0.f;
  float tape_rate_ = 1.f;
  float loop_pos_ = 0.f;
  float captured_peak_ = 0.f;

  uint32_t write_pos_ = 0U;
  uint32_t delay_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint32_t record_length_ = kMaxBufSamples;
  uint32_t frozen_origin_ = 0U;
  uint32_t frozen_length_ = 0U;
  uint32_t loop_start_ = 0U;
  uint32_t loop_length_ = 2048U;
  uint32_t roll_retarget_counter_ = 0U;

  uint8_t mode_ = MODE_NONE;
  bool pad_held_ = false;
  bool active_ = false;
  bool arming_ = false;

  SvfState svf_left_;
  SvfState svf_right_;
  fx::OnePole delay_hpf_left_;
  fx::OnePole delay_hpf_right_;
};
