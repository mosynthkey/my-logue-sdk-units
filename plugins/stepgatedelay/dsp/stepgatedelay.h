#pragma once

/*
 * File: stepgatedelay.h
 *
 * Euclidean 1/16 gate into a tempo delay. Hold the pad to engage. On pad-down
 * the nearest 16th clock becomes relative step 0 (Ride909-style capture).
 * X = Euclidean density among 16 steps (only hits open the gate).
 * Y = delay wet. SHAPE picks the per-step gate envelope (square, decaying
 * saw, ramp, triangle, exponential). Default delay time is a dotted eighth.
 * Feedback has high/low damp; low damp is engaged by default.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class StepGateDelay : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr uint32_t kMaxDelaySamples = 72000U;
  static constexpr float kGateDuty = 0.48f;

  uint32_t getBufferSize() const override final { return kMaxDelaySamples * 2U; }

  enum
  {
    DENS = 0U,
    DWET,
    MIX,
    FEED,
    HDAMP,
    LDAMP,
    TIME,
    SHAPE,
    NUM_PARAMS
  };

  enum
  {
    TIME_16 = 0,
    TIME_8,
    TIME_8D,
    TIME_4,
    TIME_4D,
    TIME_2,
    NUM_TIMES
  };

  enum
  {
    SHAPE_SQR = 0, // rectangular pulse
    SHAPE_SAW,     // decaying saw (open → closed over the step)
    SHAPE_RAMP,    // rising ramp
    SHAPE_TRI,     // triangle
    SHAPE_EXP,     // quadratic decay (softer than SAW)
    NUM_SHAPES
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DENS:
      dens_norm_ = param_10bit_to_f32(value);
      break;
    case DWET:
      dwet_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case FEED:
      feed_norm_ = param_10bit_to_f32(value);
      break;
    case HDAMP:
      hdamp_norm_ = param_10bit_to_f32(value);
      break;
    case LDAMP:
      ldamp_norm_ = param_10bit_to_f32(value);
      break;
    case TIME:
      time_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(NUM_TIMES - 1)));
      break;
    case SHAPE:
      shape_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(NUM_SHAPES - 1)));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == TIME)
    {
      static const char *kTimeNames[NUM_TIMES] = {"1/16", "1/8", "1/8D", "1/4", "1/4D", "1/2"};
      if (value < 0)
        value = 0;
      if (value >= NUM_TIMES)
        value = NUM_TIMES - 1;
      return kTimeNames[value];
    }
    if (index == SHAPE)
    {
      static const char *kShapeNames[NUM_SHAPES] = {"SQR", "SAW", "RAMP", "TRI", "EXP"};
      if (value < 0)
        value = 0;
      if (value >= NUM_SHAPES)
        value = NUM_SHAPES - 1;
      return kShapeNames[value];
    }
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    delay_left_ = allocated_buffer;
    delay_right_ = allocated_buffer != nullptr ? allocated_buffer + kMaxDelaySamples : nullptr;
    if (allocated_buffer != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxDelaySamples * 2U; ++sampleIndex)
        allocated_buffer[sampleIndex] = 0.f;
    }
    bpm_ = 120.f;
    dens_norm_ = 0.45f;
    dwet_norm_ = 0.55f;
    mix_ = 1.f;
    feed_norm_ = 0.55f;
    hdamp_norm_ = 0.18f;
    ldamp_norm_ = 0.42f; // default: low damp engaged
    time_sel_ = TIME_8D;
    shape_sel_ = SHAPE_SQR;
    reset();
  }

  void teardown() override final
  {
    delay_left_ = nullptr;
    delay_right_ = nullptr;
  }

  void reset() override final
  {
    delay_write_ = 0U;
    samples_since_tick_ = 0.f;
    samples_into_step_ = 0.f;
    next_step_ = 0U;
    gate_ = 1.f;
    wet_ = 0.f;
    pad_held_ = false;
    running_ = false;
    use_host_clock_ = false;
    have_seen_tick_ = false;
    step_open_ = true;
    fb_lp_left_.z = 0.f;
    fb_lp_right_.z = 0.f;
    fb_hp_left_.z = 0.f;
    fb_hp_right_.z = 0.f;
    if (delay_left_ != nullptr && delay_right_ != nullptr)
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
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t) override final
  {
    use_host_clock_ = true;
    onClockTick();
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      if (!running_)
      {
        syncToNearestClockAsStep0();
        running_ = true;
      }
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      running_ = false;
      step_open_ = true;
      gate_ = 1.f;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float step_samples = samplesPerTick();
    const float gate_smooth = 1.f / 64.f;
    const float wet_smooth = 1.f / 96.f;

    float delay_samples = timeBeats() * 60.f / bpm_ * getSampleRate();
    if (delay_samples < 64.f)
      delay_samples = 64.f;
    if (delay_samples > static_cast<float>(kMaxDelaySamples - 4U))
      delay_samples = static_cast<float>(kMaxDelaySamples - 4U);
    const uint32_t delay_int = static_cast<uint32_t>(delay_samples);

    const float lp_hz = 12000.f - hdamp_norm_ * hdamp_norm_ * 11000.f;
    const float hp_hz = 40.f + ldamp_norm_ * ldamp_norm_ * 900.f;
    const float lp_coeff = fx::onePoleCoeff(lp_hz, getSampleRate());
    const float hp_coeff = fx::onePoleCoeff(hp_hz, getSampleRate());
    const float feedback = 0.05f + feed_norm_ * 0.9f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      samples_since_tick_ += 1.f;
      samples_into_step_ += 1.f;
      if (!use_host_clock_)
        advanceInternalClockOneSample();

      wet_ += ((pad_held_ ? 1.f : 0.f) - wet_) * wet_smooth;

      if (running_ && step_samples > 1.f)
      {
        float phase = samples_into_step_ / step_samples;
        if (phase < 0.f)
          phase = 0.f;
        if (phase > 1.f)
          phase = 1.f;
        const float target = gateShape(phase, step_open_);
        gate_ += (target - gate_) * gate_smooth;
      }
      else
      {
        gate_ += (1.f - gate_) * gate_smooth;
      }

      const float gated_left = live_left * gate_;
      const float gated_right = live_right * gate_;

      float delayed_left = 0.f;
      float delayed_right = 0.f;
      if (delay_left_ != nullptr && delay_right_ != nullptr)
      {
        const uint32_t read_pos =
            (delay_write_ + kMaxDelaySamples - delay_int) % kMaxDelaySamples;
        delayed_left = delay_left_[read_pos];
        delayed_right = delay_right_[read_pos];

        float fb_left = fb_lp_left_.processLp(delayed_left, lp_coeff);
        float fb_right = fb_lp_right_.processLp(delayed_right, lp_coeff);
        fb_left = fb_hp_left_.processHp(fb_left, hp_coeff);
        fb_right = fb_hp_right_.processHp(fb_right, hp_coeff);
        fb_left = fx::softclip(fb_left);
        fb_right = fx::softclip(fb_right);

        delay_left_[delay_write_] = gated_left + fb_left * feedback;
        delay_right_[delay_write_] = gated_right + fb_right * feedback;
        ++delay_write_;
        if (delay_write_ >= kMaxDelaySamples)
          delay_write_ = 0U;
      }

      const float wet_line = fx::mix(gated_left, delayed_left, dwet_norm_);
      const float wet_line_r = fx::mix(gated_right, delayed_right, dwet_norm_);
      const float wet_amt = wet_ * mix_;
      out[0] = fx::mix(live_left, wet_line, wet_amt);
      out[1] = fx::mix(live_right, wet_line_r, wet_amt);

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  uint32_t debugNextStep() const { return next_step_; }
  bool debugHaveSeenTick() const { return have_seen_tick_; }

private:
  float samplesPerTick() const
  {
    if (bpm_ <= 0.f)
      return 0.f;
    return getSampleRate() * 60.f / (bpm_ * 4.f);
  }

  float timeBeats() const
  {
    static const float kBeats[NUM_TIMES] = {0.25f, 0.5f, 0.75f, 1.f, 1.5f, 2.f};
    return kBeats[time_sel_ < NUM_TIMES ? time_sel_ : TIME_8D];
  }

  float gateShape(float phase, bool hit) const
  {
    if (!hit)
      return 0.f;

    switch (shape_sel_)
    {
    case SHAPE_SAW:
      // Decaying saw: open at the step start, closed by the end.
      return 1.f - phase;
    case SHAPE_RAMP:
      return phase;
    case SHAPE_TRI:
      return (phase < 0.5f) ? (phase * 2.f) : (2.f - phase * 2.f);
    case SHAPE_EXP:
    {
      // Quadratic decay — softer than linear SAW, still libm-free.
      const float remain = 1.f - phase;
      return remain * remain;
    }
    case SHAPE_SQR:
    default:
      return (phase < kGateDuty) ? 1.f : 0.f;
    }
  }

  void applyStep(uint32_t step_index)
  {
    samples_into_step_ = 0.f;
    const uint32_t hits = 1U + static_cast<uint32_t>(dens_norm_ * static_cast<float>(kSteps - 1U));
    step_open_ = fx::euclidHit(step_index % kSteps, hits, kSteps);
  }

  void syncToNearestClockAsStep0()
  {
    const float samples_per_tick = samplesPerTick();
    if (!have_seen_tick_ || samples_per_tick <= 0.f)
    {
      // No grid yet — the next clock pulse becomes step 0.
      next_step_ = 0U;
      step_open_ = false;
      return;
    }

    float since = samples_since_tick_;
    if (since > samples_per_tick)
      since = samples_per_tick;
    const float until_next = samples_per_tick - since;

    if (until_next < since)
    {
      // Closer to the upcoming clock → that tick is step 0.
      next_step_ = 0U;
      step_open_ = false;
      return;
    }

    // Closer to the previous clock → treat it as step 0 (late pad).
    next_step_ = 1U;
    applyStep(0U);
  }

  void onClockTick()
  {
    samples_since_tick_ = 0.f;
    have_seen_tick_ = true;
    if (!running_)
      return;

    applyStep(next_step_);
    next_step_ = (next_step_ + 1U) % kSteps;
  }

  void advanceInternalClockOneSample()
  {
    const float samples_per_tick = samplesPerTick();
    if (samples_per_tick <= 0.f)
      return;
    if (samples_since_tick_ >= samples_per_tick)
      onClockTick();
  }

  float *delay_left_ = nullptr;
  float *delay_right_ = nullptr;
  uint32_t delay_write_ = 0U;

  float bpm_ = 120.f;
  float dens_norm_ = 0.45f;
  float dwet_norm_ = 0.55f;
  float mix_ = 1.f;
  float feed_norm_ = 0.55f;
  float hdamp_norm_ = 0.18f;
  float ldamp_norm_ = 0.42f;
  float samples_since_tick_ = 0.f;
  float samples_into_step_ = 0.f;
  float gate_ = 1.f;
  float wet_ = 0.f;

  uint32_t next_step_ = 0U;
  uint8_t time_sel_ = TIME_8D;
  uint8_t shape_sel_ = SHAPE_SQR;
  bool pad_held_ = false;
  bool running_ = false;
  bool use_host_clock_ = false;
  bool have_seen_tick_ = false;
  bool step_open_ = true;

  fx::OnePole fb_lp_left_;
  fx::OnePole fb_lp_right_;
  fx::OnePole fb_hp_left_;
  fx::OnePole fb_hp_right_;
};
