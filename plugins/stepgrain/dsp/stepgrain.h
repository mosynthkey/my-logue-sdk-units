#pragma once

/*
 * File: stepgrain.h
 *
 * Live capture granular pad for NTS-3.
 *
 * Always records AUDIO IN; touch freezes and granulates.
 * X/FEEL: left = sparse stitches; right = dense multi-grain wash.
 * Y = octave mix (0 / +1 / +2). MIX (Depth) blends live input with grains.
 * MODE: Volume (gain) or Freq (dry LPF + wet HPF). STEPS = body/grid; ENV = seam.
 * SPRD = stereo width. REVS = reverse probability.
 * Prefers get_raw_input while pad up.
 */

#include "fx_dsp.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StepGrain : public Processor
{
public:
  static constexpr uint32_t kMaxCaptureSamples = 144000U;
  static constexpr uint32_t kMinCaptureSamples = 2048U;
  static constexpr uint32_t kMaxGrains = 8U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kMinCapturePeak = 0.003f;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr uint8_t kNumPeriods = 8U;
  static constexpr uint8_t kNumSeams = 5U;
  static constexpr uint8_t kNumModes = 2U;

  uint32_t getBufferSize() const override final { return kMaxCaptureSamples * 2U; }

  enum
  {
    FEEL = 0U,
    OCT,
    MIX,
    MODE,
    STEPS,
    ENV,
    SPRD,
    REVS,
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
    SEAM_OFF = 0U,
    SEAM_HALF,
    SEAM_1STEP,
    SEAM_2STEP,
    SEAM_4STEP
  };

  enum
  {
    MODE_VOLUME = 0U,
    MODE_FREQ
  };

  // Match dummy-genericfx: inline set/get + switch for string params.
  inline void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case FEEL:
      feel_norm_ = param10BitToNorm(value);
      break;
    case OCT:
      oct_norm_ = param10BitToNorm(value);
      break;
    case MIX:
      mix_ = value / 100.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      updateCrossoverCoeff();
      break;
    case MODE:
      // strings type parameter, receiving index value
      mode_sel_ = static_cast<uint8_t>(
          fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumModes - 1U)));
      updateCrossoverCoeff();
      break;
    case STEPS:
      // strings type parameter, receiving index value
      period_sel_ = static_cast<uint8_t>(
          fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumPeriods - 1U)));
      break;
    case ENV:
      // strings type parameter, receiving index value
      seam_sel_ = static_cast<uint8_t>(
          fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumSeams - 1U)));
      break;
    case SPRD:
      sprd_norm_ = value / 100.f;
      if (sprd_norm_ < 0.f)
        sprd_norm_ = 0.f;
      if (sprd_norm_ > 1.f)
        sprd_norm_ = 1.f;
      break;
    case REVS:
      revs_norm_ = value / 100.f;
      if (revs_norm_ < 0.f)
        revs_norm_ = 0.f;
      if (revs_norm_ > 1.f)
        revs_norm_ = 1.f;
      break;
    default:
      break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    // Note: String memory must be accessible even after function returned.
    //       It can be assumed that caller will have copied or used the string
    //       before the next call to getParameterStrValue
    // NTS-3 string charset: A-Z a-z 0-9 space - _  (no '/')
    static const char *env_strings[kNumSeams] = {
        "Off",
        "Half",
        "1 St",
        "2 St",
        "4 St",
    };
    static const char *steps_strings[kNumPeriods] = {
        "4 Bar",
        "2 Bar",
        "16 St",
        "8 St",
        "4 St",
        "2 St",
        "1 St",
        "Half",
    };
    static const char *mode_strings[kNumModes] = {
        "Volume",
        "Freq",
    };

    switch (index)
    {
    case MODE:
      if (value >= MODE_VOLUME && value < static_cast<int32_t>(kNumModes))
        return mode_strings[value];
      break;
    case STEPS:
      if (value >= PERIOD_4BAR && value < static_cast<int32_t>(kNumPeriods))
        return steps_strings[value];
      break;
    case ENV:
      if (value >= SEAM_OFF && value < static_cast<int32_t>(kNumSeams))
        return env_strings[value];
      break;
    default:
      break;
    }

    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_left_ = allocated_buffer;
    buf_right_ = allocated_buffer + kMaxCaptureSamples;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    feel_norm_ = 1.f;
    oct_norm_ = 0.5f;
    mix_ = 1.f;
    seam_sel_ = SEAM_4STEP;
    period_sel_ = PERIOD_1STEP;
    sprd_norm_ = 1.f;
    mode_sel_ = MODE_FREQ;
    revs_norm_ = 0.5f;
    bpm_ = 120.f;
    capture_length_ = kMaxCaptureSamples;
    updateCrossoverCoeff();
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
    captured_samples_ = 0U;
    samples_since_unfreeze_ = 0U;
    arm_samples_ = 0U;
    captured_peak_ = 0.f;
    frozen_ = false;
    pad_held_ = false;
    arming_ = false;
    wet_ = 0.f;
    freeze_origin_ = 0U;
    freeze_length_ = capture_length_;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    half_step_samples_left_ = -1;
    use_host_clock_ = false;
    rng_state_ = 0xC0FFEE01U;
    dry_lpf_left_ = 0.f;
    dry_lpf_right_ = 0.f;
    wet_lpf_left_ = 0.f;
    wet_lpf_right_ = 0.f;
    clearGrains();
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    use_host_clock_ = true;
    onSixteenthTick(counter);
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;

    feel_norm_ = static_cast<float>(x) * (1.f / 1023.f);
    oct_norm_ = static_cast<float>(y) * (1.f / 1023.f);

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      arming_ = false;
      wet_ = 0.f;
      half_step_samples_left_ = -1;
      if (frozen_)
      {
        frozen_ = false;
        captured_peak_ = 0.f;
        samples_since_unfreeze_ = 0U;
        clearGrains();
      }
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    if (pad_held_)
      return;

    pad_held_ = true;
    if (hasUsableCapture())
    {
      freezeCapture();
      wet_ = 1.f;
      return;
    }

    arming_ = true;
    arm_samples_ = 0U;
    captured_peak_ = 0.f;
    wet_ = 0.f;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advanceHalfStepArm();

      float live_left = in[0];
      float live_right = in[1];
      float rec_left = live_left;
      float rec_right = live_right;
      // NTS-3 mutes unit_render input while the pad is up; raw keeps AUDIO IN.
      if (raw != nullptr)
      {
        rec_left = raw[0];
        rec_right = raw[1];
        if (pad_held_)
        {
          live_left = raw[0];
          live_right = raw[1];
        }
      }

      if (!frozen_)
      {
        recordSample(rec_left, rec_right);
        if (samples_since_unfreeze_ < 0xFFFFFFF0U)
          ++samples_since_unfreeze_;
        if (arming_ && arm_samples_ < 0xFFFFFFF0U)
          ++arm_samples_;
        if (arming_ && arm_samples_ >= armReadySamples() && captured_peak_ >= kMinCapturePeak)
        {
          freezeCapture();
          wet_ = 1.f;
          arming_ = false;
        }
      }

      float grain_left = 0.f;
      float grain_right = 0.f;
      if (wet_ > 0.f && frozen_)
        renderGrains(grain_left, grain_right);

      if (wet_ <= 0.f)
      {
        out[0] = live_left;
        out[1] = live_right;
      }
      else if (mode_sel_ == MODE_FREQ)
      {
        // Complementary crossover: dry = LPF(input), wet = HPF(grains), then sum.
        dry_lpf_left_ += xo_coeff_ * (live_left - dry_lpf_left_);
        dry_lpf_right_ += xo_coeff_ * (live_right - dry_lpf_right_);
        wet_lpf_left_ += xo_coeff_ * (grain_left - wet_lpf_left_);
        wet_lpf_right_ += xo_coeff_ * (grain_right - wet_lpf_right_);
        out[0] = dry_lpf_left_ + (grain_left - wet_lpf_left_);
        out[1] = dry_lpf_right_ + (grain_right - wet_lpf_right_);
      }
      else
      {
        const float dry = 1.f - mix_;
        out[0] = live_left * dry + grain_left * mix_;
        out[1] = live_right * dry + grain_right * mix_;
      }

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  struct Grain
  {
    bool active = false;
    float read_pos = 0.f;
    float rate = 1.f;
    float gain = 1.f;
    float pan_left = 0.7071f;
    float pan_right = 0.7071f;
    uint32_t age = 0U;
    uint32_t length = 0U;
    uint32_t attack = 8U;
    uint32_t release = 8U;

    void reset() { active = false; }

    void trigger(float pos, float playback_rate, float velocity, uint32_t length_samples,
                 uint32_t attack_samples, uint32_t release_samples, float gain_left, float gain_right)
    {
      active = true;
      read_pos = pos;
      rate = playback_rate;
      gain = velocity;
      pan_left = gain_left;
      pan_right = gain_right;
      age = 0U;
      length = length_samples < 32U ? 32U : length_samples;
      attack = attack_samples < 1U ? 1U : attack_samples;
      release = release_samples < 1U ? 1U : release_samples;
      if (attack + release > length)
      {
        attack = length / 2U;
        release = length - attack;
        if (attack < 1U)
          attack = 1U;
        if (release < 1U)
          release = 1U;
      }
    }
  };

  static float param10BitToNorm(int32_t value)
  {
    return static_cast<uint16_t>(value) * (1.f / 1023.f);
  }

  static float clamp01(float value)
  {
    if (value < 0.f)
      return 0.f;
    if (value > 1.f)
      return 1.f;
    return value;
  }

  static float absf(float value) { return value < 0.f ? -value : value; }

  static float hermite(float y0, float y1, float y2, float y3, float frac)
  {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
  }

  static float periodSixteenths(uint8_t period_sel)
  {
    static const float kPeriods[kNumPeriods] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    return kPeriods[period_sel < kNumPeriods ? period_sel : PERIOD_1STEP];
  }

  static float seamSixteenths(uint8_t seam_sel)
  {
    static const float kSeams[kNumSeams] = {0.f, 0.5f, 1.f, 2.f, 4.f};
    return kSeams[seam_sel < kNumSeams ? seam_sel : SEAM_OFF];
  }

  // Raised-cosine fade over the seam margins stored on each grain.
  static float grainEnvelope(uint32_t age, uint32_t length, uint32_t attack, uint32_t release)
  {
    if (length <= 1U)
      return 0.f;

    if (age < attack)
    {
      const float phase = static_cast<float>(age) / static_cast<float>(attack);
      return 0.5f * (1.f - fastercosfullf(phase * 3.14159265f));
    }
    if (age >= length - release)
    {
      const float phase = static_cast<float>(length - age) / static_cast<float>(release);
      return 0.5f * (1.f - fastercosfullf(phase * 3.14159265f));
    }
    return 1.f;
  }

  static uint32_t wrapIndex(uint32_t index, uint32_t length)
  {
    if (length == 0U)
      return 0U;
    while (index >= length)
      index -= length;
    return index;
  }

  uint32_t armReadySamples() const
  {
    uint32_t ready = static_cast<uint32_t>(getSampleRate() * 0.25f);
    if (ready < kMinCaptureSamples)
      ready = kMinCaptureSamples;
    return ready;
  }

  bool hasUsableCapture() const
  {
    const uint32_t ready = armReadySamples();
    return captured_samples_ >= kMinCaptureSamples && captured_peak_ >= kMinCapturePeak &&
           samples_since_unfreeze_ >= ready;
  }

  uint32_t periodLengthSamples() const
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    float samples = beat * 0.25f * periodSixteenths(period_sel_);
    if (samples < 48.f)
      samples = 48.f;
    return static_cast<uint32_t>(samples + 0.5f);
  }

  uint32_t seamMarginSamples() const
  {
    const float sixteenths = seamSixteenths(seam_sel_);
    if (sixteenths <= 0.f)
      return 8U;
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    float samples = beat * 0.25f * sixteenths;
    if (samples < 8.f)
      samples = 8.f;
    return static_cast<uint32_t>(samples + 0.5f);
  }

  // MIX maps crossover Hz: 0% → open (~10 kHz, mostly dry), 100% → low (~60 Hz, mostly wet).
  void updateCrossoverCoeff()
  {
    const float hz = 60.f * fasterpowf(10000.f / 60.f, 1.f - clamp01(mix_));
    const float x = -kTwoPi * hz / getSampleRate();
    xo_coeff_ = (-x < 0.08f) ? -x : (1.f - fasterexpf(x));
  }

  void freezeCapture()
  {
    capture_length_ = kMaxCaptureSamples;
    frozen_ = true;
    freeze_length_ = capture_length_;
    if (captured_samples_ < freeze_length_)
      freeze_length_ = captured_samples_;
    if (freeze_length_ < kMinCaptureSamples)
      freeze_length_ = kMinCaptureSamples;

    const uint32_t newest_index = write_pos_ == 0U ? kMaxCaptureSamples - 1U : write_pos_ - 1U;
    int32_t origin = static_cast<int32_t>(newest_index) - static_cast<int32_t>(freeze_length_) + 1;
    if (origin < 0)
      origin += static_cast<int32_t>(kMaxCaptureSamples);
    freeze_origin_ = static_cast<uint32_t>(origin);

    half_step_samples_left_ = -1;
    clearGrains();
    // Engage immediately; subsequent spawns follow the absolute grid.
    fireStepGrains();
  }

  void recordSample(float left, float right)
  {
    if (buf_left_ == nullptr)
      return;

    buf_left_[write_pos_] = left;
    buf_right_[write_pos_] = right;
    ++write_pos_;
    if (write_pos_ >= kMaxCaptureSamples)
      write_pos_ = 0U;

    if (captured_samples_ < kMaxCaptureSamples)
      ++captured_samples_;

    const float peak = absf(left) > absf(right) ? absf(left) : absf(right);
    if (peak > captured_peak_)
      captured_peak_ = peak;
  }

  void clearGrains()
  {
    for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
      grains_[grainIndex].reset();
  }

  float nextUnitRandom()
  {
    rng_state_ = rng_state_ * 1664525U + 1013904223U;
    return static_cast<float>(rng_state_ >> 8) * (1.f / 16777216.f);
  }

  float randomSigned() { return nextUnitRandom() * 2.f - 1.f; }

  // Y maps 0..1 → mixture of unison / +1 / +2 octave grains (rates 1 / 2 / 4).
  float chooseGrainRate()
  {
    const float target = clamp01(oct_norm_) * 2.f;
    const float lo = static_cast<float>(static_cast<int32_t>(target));
    float hi = lo + 1.f;
    if (hi > 2.f)
      hi = 2.f;
    const float frac = target - lo;
    const float octaves = (nextUnitRandom() < frac) ? hi : lo;
    float rate = 1.f;
    if (octaves >= 1.5f)
      rate = 4.f;
    else if (octaves >= 0.5f)
      rate = 2.f;

    if (nextUnitRandom() < clamp01(revs_norm_))
      rate = -rate;
    return rate;
  }

  void onSixteenthTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!frozen_ || wet_ <= 0.f)
    {
      half_step_samples_left_ = -1;
      return;
    }

    const float period = periodSixteenths(period_sel_);
    if (period < 1.f)
    {
      // PERIOD_HALF: fire on each 16th and arm a mid-16th (32nd) spawn.
      fireStepGrains();
      const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
      half_step_samples_left_ = static_cast<int32_t>(beat * 0.125f);
      return;
    }

    const uint32_t period_ticks = static_cast<uint32_t>(period + 0.5f);
    if (period_ticks == 0U)
      return;
    const uint32_t step_index = (counter == 0U) ? 0U : (counter - 1U);
    if ((step_index % period_ticks) == 0U)
      fireStepGrains();
  }

  void advanceInternalClockOneSample()
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    const float samples_per_tick = beat * 0.25f;
    if (samples_per_tick <= 0.f)
      return;
    internal_tick_phase_ += 1.f;
    if (internal_tick_phase_ >= samples_per_tick)
    {
      internal_tick_phase_ -= samples_per_tick;
      ++tick_counter_;
      onSixteenthTick(tick_counter_);
    }
  }

  void advanceHalfStepArm()
  {
    if (half_step_samples_left_ < 0)
      return;
    --half_step_samples_left_;
    if (half_step_samples_left_ == 0)
    {
      half_step_samples_left_ = -1;
      if (frozen_ && wet_ > 0.f)
        fireStepGrains();
    }
  }

  void fireStepGrains()
  {
    if (freeze_length_ < kMinCaptureSamples)
      return;

    const float x = clamp01(feel_norm_);
    const float smooth = x * x * (3.f - 2.f * x);
    // Sparse: 1 grain per step. Dense: up to 6 overlapping grains.
    const uint32_t grain_count = 1U + static_cast<uint32_t>(smooth * 5.f + 0.5f);
    for (uint32_t spawnIndex = 0; spawnIndex < grain_count; ++spawnIndex)
      spawnGrain(smooth, grain_count);
  }

  void spawnGrain(float smooth, uint32_t grain_count)
  {
    uint32_t slot = kMaxGrains;
    for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
    {
      if (!grains_[grainIndex].active)
      {
        slot = grainIndex;
        break;
      }
    }
    if (slot >= kMaxGrains)
    {
      float best_progress = 0.65f;
      uint32_t best_slot = kMaxGrains;
      for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
      {
        const Grain &grain = grains_[grainIndex];
        if (!grain.active || grain.length == 0U)
          continue;
        const float progress = static_cast<float>(grain.age) / static_cast<float>(grain.length);
        if (progress > best_progress)
        {
          best_progress = progress;
          best_slot = grainIndex;
        }
      }
      if (best_slot >= kMaxGrains)
        return;
      slot = best_slot;
    }

    uint32_t body_samples = periodLengthSamples();
    if (body_samples < 48U)
      body_samples = 48U;

    // ENV selects fade-in/out length in steps (のりしろ) beyond the STEPS body.
    uint32_t margin = seamMarginSamples();
    uint32_t length_samples = body_samples + 2U * margin;
    if (freeze_length_ >= 32U && length_samples > freeze_length_)
    {
      length_samples = freeze_length_;
      margin = (length_samples - 16U) / 2U;
      if (margin < 8U)
        margin = 8U;
      if (2U * margin >= length_samples)
        margin = length_samples / 4U;
      if (margin < 1U)
        margin = 1U;
    }

    const float spray = 0.05f + smooth * 0.30f;
    const float center = 0.70f + randomSigned() * spray;
    float wrapped = center;
    while (wrapped >= 1.f)
      wrapped -= 1.f;
    while (wrapped < 0.f)
      wrapped += 1.f;

    float read_pos = wrapped * static_cast<float>(freeze_length_);
    const float rate = chooseGrainRate();
    if (rate < 0.f)
    {
      read_pos += static_cast<float>(length_samples);
      while (read_pos >= static_cast<float>(freeze_length_))
        read_pos -= static_cast<float>(freeze_length_);
    }

    const float body_six = periodSixteenths(period_sel_);
    const float seam_six = seamSixteenths(seam_sel_);
    const float temporal = (body_six > 0.f) ? (seam_six / body_six) : 0.f;
    float overlap = static_cast<float>(grain_count) * (1.f + temporal);
    if (overlap < 1.f)
      overlap = 1.f;
    const float density_gain = 1.85f / fasterpowf(overlap, 0.5f);
    const float velocity = (0.75f + nextUnitRandom() * 0.35f) * density_gain;

    const float pan = randomSigned() * clamp01(sprd_norm_);
    const float angle = (pan + 1.f) * 0.7853981633974483f;
    const float gain_left = fastercosf(angle);
    const float gain_right = fastersinf(angle);

    grains_[slot].trigger(read_pos, rate, velocity, length_samples, margin, margin, gain_left, gain_right);
  }

  void sampleFrozen(float position, float &left, float &right) const
  {
    const uint32_t length = freeze_length_ == 0U ? 1U : freeze_length_;
    float pos = position;
    while (pos >= static_cast<float>(length))
      pos -= static_cast<float>(length);
    while (pos < 0.f)
      pos += static_cast<float>(length);

    const int32_t index = static_cast<int32_t>(pos);
    const float frac = pos - static_cast<float>(index);
    const uint32_t i0 = wrapIndex(
        static_cast<uint32_t>((index - 1 + static_cast<int32_t>(length)) % static_cast<int32_t>(length)),
        length);
    const uint32_t i1 = wrapIndex(static_cast<uint32_t>(index), length);
    const uint32_t i2 = wrapIndex(i1 + 1U, length);
    const uint32_t i3 = wrapIndex(i2 + 1U, length);

    const uint32_t a0 = wrapIndex(freeze_origin_ + i0, kMaxCaptureSamples);
    const uint32_t a1 = wrapIndex(freeze_origin_ + i1, kMaxCaptureSamples);
    const uint32_t a2 = wrapIndex(freeze_origin_ + i2, kMaxCaptureSamples);
    const uint32_t a3 = wrapIndex(freeze_origin_ + i3, kMaxCaptureSamples);

    left = hermite(buf_left_[a0], buf_left_[a1], buf_left_[a2], buf_left_[a3], frac);
    right = hermite(buf_right_[a0], buf_right_[a1], buf_right_[a2], buf_right_[a3], frac);
  }

  void renderGrains(float &left, float &right)
  {
    left = 0.f;
    right = 0.f;

    for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
    {
      Grain &grain = grains_[grainIndex];
      if (!grain.active)
        continue;

      float sample_left = 0.f;
      float sample_right = 0.f;
      sampleFrozen(grain.read_pos, sample_left, sample_right);

      const float envelope = grainEnvelope(grain.age, grain.length, grain.attack, grain.release);
      const float amp = grain.gain * envelope;
      const float mono = (sample_left + sample_right) * 0.5f * amp;
      left += mono * grain.pan_left;
      right += mono * grain.pan_right;

      grain.read_pos += grain.rate;
      const float freeze_f = static_cast<float>(freeze_length_ == 0U ? 1U : freeze_length_);
      while (grain.read_pos >= freeze_f)
        grain.read_pos -= freeze_f;
      while (grain.read_pos < 0.f)
        grain.read_pos += freeze_f;

      ++grain.age;
      if (grain.age >= grain.length)
        grain.active = false;
    }

    left = fastertanhf(left * 1.15f);
    right = fastertanhf(right * 1.15f);
  }

  float *buf_left_ = nullptr;
  float *buf_right_ = nullptr;

  float feel_norm_ = 1.f;
  float oct_norm_ = 0.5f;
  float mix_ = 1.f;
  float sprd_norm_ = 1.f;
  float revs_norm_ = 0.5f;
  uint8_t period_sel_ = PERIOD_1STEP;
  uint8_t seam_sel_ = SEAM_4STEP;
  uint8_t mode_sel_ = MODE_FREQ;
  float bpm_ = 120.f;

  uint32_t write_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint32_t samples_since_unfreeze_ = 0U;
  uint32_t arm_samples_ = 0U;
  uint32_t capture_length_ = kMaxCaptureSamples;
  uint32_t freeze_origin_ = 0U;
  uint32_t freeze_length_ = kMaxCaptureSamples;
  float captured_peak_ = 0.f;
  bool frozen_ = false;
  bool pad_held_ = false;
  bool arming_ = false;

  float wet_ = 0.f;
  uint32_t tick_counter_ = 0U;
  float internal_tick_phase_ = 0.f;
  int32_t half_step_samples_left_ = -1;
  bool use_host_clock_ = false;
  uint32_t rng_state_ = 0xC0FFEE01U;
  Grain grains_[kMaxGrains];

  float xo_coeff_ = 0.05f;
  float dry_lpf_left_ = 0.f;
  float dry_lpf_right_ = 0.f;
  float wet_lpf_left_ = 0.f;
  float wet_lpf_right_ = 0.f;
};
