#pragma once

/*
 * File: steperror.h
 *
 * DataBend-style media-failure errors, decided once per tempo step.
 * X = fire probability, Y = error depth. Pad engages; release is dry.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StepError : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 48000U;
  static constexpr uint32_t kXfadeSamples = 64U;
  static constexpr uint32_t kMinSkipSamples = 48U;
  static constexpr uint32_t kMaxSkipSamples = 2048U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr uint8_t kNumPeriods = 8U;
  static constexpr uint8_t kNumKinds = 5U;

  uint32_t getBufferSize() const override final { return kMaxBufSamples * 2U; }

  enum
  {
    PROB = 0U,
    ERR,
    MIX,
    STEPS,
    KIND,
    NUM_PARAMS
  };

  enum
  {
    PERIOD_4BAR = 0U,
    PERIOD_2BAR,
    PERIOD_16STEP,
    PERIOD_8STEP,
    PERIOD_4STEP,
    PERIOD_2STEP,
    PERIOD_1STEP,
    PERIOD_HALF
  };

  enum
  {
    KIND_ALL = 0U,
    KIND_HOLD,
    KIND_CRUSH,
    KIND_DROP,
    KIND_SKIP
  };

  enum
  {
    ERR_DRY = 0U,
    ERR_HOLD,
    ERR_CRUSH,
    ERR_DROP,
    ERR_SKIP
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PROB:
      prob_ = param_10bit_to_f32(value);
      break;
    case ERR:
      err_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case STEPS:
      period_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumPeriods - 1U)));
      break;
    case KIND:
      kind_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumKinds - 1U)));
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *period_names[kNumPeriods] = {"4Bar", "2Bar", "16St", "8St", "4St", "2St", "1St", "1/2"};
    static const char *kind_names[kNumKinds] = {"ALL", "HOLD", "CRUSH", "DROP", "SKIP"};
    if (index == STEPS && value >= 0 && value < static_cast<int32_t>(kNumPeriods))
      return period_names[value];
    if (index == KIND && value >= 0 && value < static_cast<int32_t>(kNumKinds))
      return kind_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_left_ = allocated_buffer;
    buf_right_ = allocated_buffer != nullptr ? allocated_buffer + kMaxBufSamples : nullptr;
    if (allocated_buffer != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
        allocated_buffer[sampleIndex] = 0.f;
    }

    prob_ = 0.45f;
    err_ = 0.55f;
    mix_ = 1.f;
    period_sel_ = PERIOD_1STEP;
    kind_sel_ = KIND_ALL;
    bpm_ = 120.f;
    rng_ = 0x51E55E11U;
    reset();
  }

  void teardown() override final
  {
    buf_left_ = nullptr;
    buf_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    captured_ = 0U;
    clock_acc_ = 0.f;
    xfade_ = 1.f;
    pad_held_ = false;
    engaged_ = 0.f;
    active_kind_ = ERR_DRY;
    hold_left_ = 0.f;
    hold_right_ = 0.f;
    hold_timer_ = 0U;
    hold_period_ = 64U;
    crush_count_ = 0U;
    crush_period_ = 1U;
    crush_levels_ = 16.f;
    skip_start_ = 0U;
    skip_length_ = 256U;
    skip_pos_ = 0.f;
    skip_rate_ = 1.f;
    prev_wet_left_ = 0.f;
    prev_wet_right_ = 0.f;
    if (buf_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufSamples; ++sampleIndex)
      {
        buf_left_[sampleIndex] = 0.f;
        buf_right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      enterStep();
      xfade_ = 0.f;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
      pad_held_ = false;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float step_samples = periodSamples();
    const float xfade_inc = 1.f / static_cast<float>(kXfadeSamples);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (buf_left_ != nullptr)
      {
        buf_left_[write_pos_] = live_left;
        buf_right_[write_pos_] = live_right;
        write_pos_ = (write_pos_ + 1U) % kMaxBufSamples;
        if (captured_ < kMaxBufSamples)
          ++captured_;
      }

      clock_acc_ += 1.f;
      if (clock_acc_ >= step_samples)
      {
        clock_acc_ -= step_samples;
        enterStep();
        xfade_ = 0.f;
      }

      if (xfade_ < 1.f)
      {
        xfade_ += xfade_inc;
        if (xfade_ > 1.f)
          xfade_ = 1.f;
      }

      float wet_left = live_left;
      float wet_right = live_right;
      if (pad_held_ && active_kind_ != ERR_DRY)
        renderError(live_left, live_right, wet_left, wet_right);

      wet_left = fx::mix(prev_wet_left_, wet_left, xfade_);
      wet_right = fx::mix(prev_wet_right_, wet_right, xfade_);
      prev_wet_left_ = wet_left;
      prev_wet_right_ = wet_right;

      engaged_ += ((pad_held_ ? 1.f : 0.f) - engaged_) * 0.04f;
      const float amount = mix_ * fx::clip01(engaged_);
      out[0] = fx::mix(live_left, wet_left, amount);
      out[1] = fx::mix(live_right, wet_right, amount);

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  uint8_t activeKind() const { return active_kind_; }

private:
  static float periodSixteenths(uint8_t period_sel)
  {
    static const float kPeriods[kNumPeriods] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    return kPeriods[period_sel < kNumPeriods ? period_sel : PERIOD_1STEP];
  }

  float periodSamples() const
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    return beat * 0.25f * periodSixteenths(period_sel_);
  }

  static float quantize(float sample, float levels)
  {
    if (levels <= 1.f)
      return 0.f;
    const float scaled = fx::clip(sample, -1.f, 1.f) * levels;
    const float snapped = static_cast<float>(static_cast<int32_t>(scaled));
    return snapped / levels;
  }

  uint8_t rollKind()
  {
    if (kind_sel_ == KIND_HOLD)
      return ERR_HOLD;
    if (kind_sel_ == KIND_CRUSH)
      return ERR_CRUSH;
    if (kind_sel_ == KIND_DROP)
      return ERR_DROP;
    if (kind_sel_ == KIND_SKIP)
      return ERR_SKIP;

    const float pick = fx::randomFloat(rng_);
    if (pick < 0.28f)
      return ERR_HOLD;
    if (pick < 0.52f)
      return ERR_CRUSH;
    if (pick < 0.72f)
      return ERR_DROP;
    return ERR_SKIP;
  }

  void enterStep()
  {
    if (fx::randomFloat(rng_) >= prob_)
    {
      active_kind_ = ERR_DRY;
      return;
    }

    active_kind_ = rollKind();
    const float depth = err_;
    const float step_samples = periodSamples();

    hold_left_ = 0.f;
    hold_right_ = 0.f;
    if (buf_left_ != nullptr && captured_ > 0U)
    {
      const uint32_t last = (write_pos_ + kMaxBufSamples - 1U) % kMaxBufSamples;
      hold_left_ = buf_left_[last];
      hold_right_ = buf_right_[last];
    }

    const float refresh = 8.f + (1.f - depth) * (1.f - depth) * step_samples;
    hold_period_ = static_cast<uint32_t>(refresh);
    if (hold_period_ < 8U)
      hold_period_ = 8U;
    hold_timer_ = 0U;

    crush_levels_ = 2.f + (1.f - depth) * 30.f;
    crush_period_ = 1U + static_cast<uint32_t>(depth * 24.f);
    crush_count_ = 0U;

    uint32_t skip = kMinSkipSamples + static_cast<uint32_t>((1.f - depth) * static_cast<float>(kMaxSkipSamples - kMinSkipSamples));
    if (skip > captured_ && captured_ > kMinSkipSamples)
      skip = captured_;
    if (skip < kMinSkipSamples)
      skip = kMinSkipSamples;
    skip_length_ = skip;
    skip_start_ = (write_pos_ + kMaxBufSamples - skip_length_) % kMaxBufSamples;
    skip_pos_ = 0.f;
    skip_rate_ = 1.f + (fx::randomFloat(rng_) - 0.5f) * depth * 0.55f;
    if (skip_rate_ < 0.35f)
      skip_rate_ = 0.35f;
  }

  void applyScramble(float &left, float &right)
  {
    if (err_ <= 0.4f)
      return;
    if ((fx::nextRandom(rng_) & 255U) >= static_cast<uint32_t>(err_ * 40.f))
      return;
    const uint32_t scramble = fx::nextRandom(rng_);
    left = ((scramble & 1U) != 0U) ? -left : left * 0.15f;
    right = ((scramble & 2U) != 0U) ? -right : right * 0.15f;
  }

  void renderError(float live_left, float live_right, float &wet_left, float &wet_right)
  {
    switch (active_kind_)
    {
    case ERR_HOLD:
      if (hold_timer_ == 0U)
      {
        hold_left_ = live_left;
        hold_right_ = live_right;
        hold_timer_ = hold_period_;
      }
      else
      {
        --hold_timer_;
      }
      wet_left = hold_left_;
      wet_right = hold_right_;
      break;

    case ERR_CRUSH:
      if (crush_count_ == 0U)
      {
        hold_left_ = live_left;
        hold_right_ = live_right;
        crush_count_ = crush_period_;
      }
      else
      {
        --crush_count_;
      }
      wet_left = quantize(hold_left_, crush_levels_);
      wet_right = quantize(hold_right_, crush_levels_);
      break;

    case ERR_DROP:
    {
      const float leak = 1.f - err_;
      wet_left = live_left * leak;
      wet_right = live_right * leak;
      if (err_ > 0.7f && (fx::nextRandom(rng_) & 255U) < 6U)
      {
        wet_left = live_left * 0.35f;
        wet_right = live_right * 0.35f;
      }
      break;
    }

    case ERR_SKIP:
      if (buf_left_ != nullptr && captured_ >= kMinSkipSamples)
      {
        skip_pos_ += skip_rate_;
        const float length = static_cast<float>(skip_length_);
        if (skip_pos_ >= length)
          skip_pos_ -= length;
        const uint32_t index_a = (skip_start_ + static_cast<uint32_t>(skip_pos_)) % kMaxBufSamples;
        const uint32_t index_b = (index_a + 1U) % kMaxBufSamples;
        const float frac = skip_pos_ - static_cast<float>(static_cast<uint32_t>(skip_pos_));
        wet_left = buf_left_[index_a] + (buf_left_[index_b] - buf_left_[index_a]) * frac;
        wet_right = buf_right_[index_a] + (buf_right_[index_b] - buf_right_[index_a]) * frac;
      }
      else
      {
        wet_left = hold_left_;
        wet_right = hold_right_;
      }
      break;

    default:
      wet_left = live_left;
      wet_right = live_right;
      break;
    }

    applyScramble(wet_left, wet_right);
  }

  float *buf_left_ = nullptr;
  float *buf_right_ = nullptr;
  uint32_t write_pos_ = 0U;
  uint32_t captured_ = 0U;
  uint32_t rng_ = 0x51E55E11U;
  uint32_t hold_timer_ = 0U;
  uint32_t hold_period_ = 64U;
  uint32_t crush_count_ = 0U;
  uint32_t crush_period_ = 1U;
  uint32_t skip_start_ = 0U;
  uint32_t skip_length_ = 256U;
  float clock_acc_ = 0.f;
  float xfade_ = 1.f;
  float engaged_ = 0.f;
  float skip_pos_ = 0.f;
  float skip_rate_ = 1.f;
  float crush_levels_ = 16.f;
  float hold_left_ = 0.f;
  float hold_right_ = 0.f;
  float prev_wet_left_ = 0.f;
  float prev_wet_right_ = 0.f;
  float prob_ = 0.45f;
  float err_ = 0.55f;
  float mix_ = 1.f;
  float bpm_ = 120.f;
  uint8_t period_sel_ = PERIOD_1STEP;
  uint8_t kind_sel_ = KIND_ALL;
  uint8_t active_kind_ = ERR_DRY;
  bool pad_held_ = false;
};
