#pragma once

/*
 * File: stepgrain2.h
 *
 * Live-capture granular pad for NTS-3.
 *
 * Touch freezes up to 3 s of AUDIO IN. Grains start at 70% of that window.
 * The clock is every 16th note. Probability is the chance a grain fires.
 * Length is the whole grain (1/2/4/8/16 steps). Fade sits inside that length:
 * each side is Fade% of half the grain, with at least a few samples of edge.
 * Shimmer brings in +1 oct first, then +2 oct. Mix is a volume crossfade.
 */

#include "fx_dsp.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StepGrain2 : public Processor
{
public:
  static constexpr uint32_t kMaxCaptureSamples = 144000U;
  static constexpr uint32_t kMinCaptureSamples = 2048U;
  static constexpr uint32_t kMaxGrains = 12U;
  static constexpr uint32_t kMinEdgeSamples = 8U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kMinCapturePeak = 0.003f;
  static constexpr uint8_t kNumLengths = 5U;

  uint32_t getBufferSize() const override final { return kMaxCaptureSamples; }

  enum
  {
    FADE = 0U,
    PROBABILITY,
    MIX,
    LENGTH,
    SHIMMER,
    SPREAD,
    REVERSE,
    NUM_PARAMS
  };

  enum
  {
    LENGTH_1 = 0U,
    LENGTH_2,
    LENGTH_4,
    LENGTH_8,
    LENGTH_16
  };

  inline void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case FADE:
      fade_norm_ = percentToNorm(value);
      break;
    case PROBABILITY:
      probability_norm_ = percentToNorm(value);
      break;
    case MIX:
      mix_ = percentToNorm(value);
      break;
    case LENGTH:
      length_sel_ = static_cast<uint8_t>(
          fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumLengths - 1U)));
      break;
    case SHIMMER:
      shimmer_norm_ = percentToNorm(value);
      break;
    case SPREAD:
      spread_norm_ = percentToNorm(value);
      break;
    case REVERSE:
      reverse_norm_ = percentToNorm(value);
      break;
    default:
      break;
    }
  }

  inline const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *length_strings[kNumLengths] = {
        "1 Step",
        "2 Step",
        "4 Step",
        "8 Step",
        "16 Step",
    };

    if (index == LENGTH && value >= LENGTH_1 && value < static_cast<int32_t>(kNumLengths))
      return length_strings[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_ = allocated_buffer;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    fade_norm_ = 0.5f;
    probability_norm_ = 1.f;
    mix_ = 1.f;
    length_sel_ = LENGTH_1;
    shimmer_norm_ = 1.f;
    spread_norm_ = 1.f;
    reverse_norm_ = 0.5f;
    bpm_ = 120.f;
    reset();
  }

  void teardown() override final { buf_ = nullptr; }

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
    freeze_length_ = kMaxCaptureSamples;
    tick_counter_ = 0U;
    internal_tick_phase_ = 0.f;
    use_host_clock_ = false;
    rng_state_ = 0xC0FFEE01U;
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

    fade_norm_ = static_cast<float>(x) * (1.f / 1023.f);
    probability_norm_ = static_cast<float>(y) * (1.f / 1023.f);

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      arming_ = false;
      wet_ = 0.f;
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

      float live_left = in[0];
      float live_right = in[1];
      float rec_left = live_left;
      float rec_right = live_right;
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

  static float percentToNorm(int32_t value)
  {
    float norm = static_cast<float>(value) * 0.01f;
    if (norm < 0.f)
      return 0.f;
    if (norm > 1.f)
      return 1.f;
    return norm;
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

  static float lengthSixteenths(uint8_t length_sel)
  {
    static const float kLengths[kNumLengths] = {1.f, 2.f, 4.f, 8.f, 16.f};
    const uint8_t index = length_sel < kNumLengths ? length_sel : static_cast<uint8_t>(LENGTH_1);
    return kLengths[index];
  }

  static float grainEnvelope(uint32_t age, uint32_t length, uint32_t attack, uint32_t release)
  {
    if (length <= 1U)
      return 0.f;

    if (age < attack)
    {
      const float phase = static_cast<float>(age) / static_cast<float>(attack);
      return phase * phase * (3.f - 2.f * phase);
    }
    if (age >= length - release)
    {
      const float phase = static_cast<float>(length - age) / static_cast<float>(release);
      return phase * phase * (3.f - 2.f * phase);
    }
    return 1.f;
  }

  static float softClip(float value)
  {
    const float x = value * 1.15f;
    return x / (1.f + absf(x));
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

  uint32_t grainLengthSamples() const
  {
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, getSampleRate()));
    float samples = beat * 0.25f * lengthSixteenths(length_sel_);
    if (samples < 48.f)
      samples = 48.f;
    return static_cast<uint32_t>(samples + 0.5f);
  }

  // Fade 0% is still a few samples, so the edge does not click.
  void insideFades(uint32_t length, uint32_t &attack, uint32_t &release) const
  {
    const float half = static_cast<float>(length) * 0.5f;
    float edge = clamp01(fade_norm_) * half;
    if (edge < static_cast<float>(kMinEdgeSamples))
      edge = static_cast<float>(kMinEdgeSamples);
    attack = static_cast<uint32_t>(edge + 0.5f);
    release = attack;
    if (attack < 1U)
      attack = 1U;
    if (release < 1U)
      release = 1U;
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

  void freezeCapture()
  {
    frozen_ = true;
    freeze_length_ = kMaxCaptureSamples;
    if (captured_samples_ < freeze_length_)
      freeze_length_ = captured_samples_;
    if (freeze_length_ < kMinCaptureSamples)
      freeze_length_ = kMinCaptureSamples;

    const uint32_t newest_index = write_pos_ == 0U ? kMaxCaptureSamples - 1U : write_pos_ - 1U;
    int32_t origin = static_cast<int32_t>(newest_index) - static_cast<int32_t>(freeze_length_) + 1;
    if (origin < 0)
      origin += static_cast<int32_t>(kMaxCaptureSamples);
    freeze_origin_ = static_cast<uint32_t>(origin);

    clearGrains();
    maybeFireGrain();
  }

  void recordSample(float left, float right)
  {
    if (buf_ == nullptr)
      return;

    buf_[write_pos_] = (left + right) * 0.5f;
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

  // 0%: original only. Up to 50%, +1 oct replaces original.
  // Above 50%, +2 oct grows until 100% is original / +1 / +2 at 1/3 each.
  float chooseGrainRate()
  {
    const float shimmer = clamp01(shimmer_norm_);
    float unison = 1.f;
    float up_one = 0.f;
    float up_two = 0.f;
    if (shimmer <= 0.5f)
    {
      up_one = shimmer;
      unison = 1.f - up_one;
    }
    else
    {
      const float toward_top = (shimmer - 0.5f) * 2.f;
      up_two = toward_top * (1.f / 3.f);
      unison = 0.5f - toward_top * (1.f / 6.f);
      up_one = unison;
    }

    const float roll = nextUnitRandom();
    float rate = 1.f;
    // up_two is the top slice. A zero width keeps +2 out until Shimmer passes 50%.
    if (up_two > 0.f && roll >= unison + up_one)
      rate = 4.f;
    else if (roll >= unison)
      rate = 2.f;

    if (nextUnitRandom() < clamp01(reverse_norm_))
      rate = -rate;
    return rate;
  }

  void onSixteenthTick(uint32_t counter)
  {
    tick_counter_ = counter;
    if (!frozen_ || wet_ <= 0.f)
      return;
    maybeFireGrain();
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

  void maybeFireGrain()
  {
    if (freeze_length_ < kMinCaptureSamples)
      return;
    if (nextUnitRandom() >= clamp01(probability_norm_))
      return;
    spawnGrain();
  }

  void spawnGrain()
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

    uint32_t length_samples = grainLengthSamples();
    uint32_t attack = kMinEdgeSamples;
    uint32_t release = kMinEdgeSamples;
    insideFades(length_samples, attack, release);

    float read_pos = 0.70f * static_cast<float>(freeze_length_);
    const float rate = chooseGrainRate();
    if (rate < 0.f)
    {
      read_pos += static_cast<float>(length_samples);
      while (read_pos >= static_cast<float>(freeze_length_))
        read_pos -= static_cast<float>(freeze_length_);
    }

    float overlap = lengthSixteenths(length_sel_) * clamp01(probability_norm_);
    if (overlap < 1.f)
      overlap = 1.f;
    const float density_gain = 1.85f / fasterpowf(overlap, 0.5f);
    const float velocity = (0.75f + nextUnitRandom() * 0.35f) * density_gain;

    const float pan = randomSigned() * clamp01(spread_norm_);
    const float angle = (pan + 1.f) * 0.7853981633974483f;
    const float gain_left = fastercosf(angle);
    const float gain_right = fastersinf(angle);

    grains_[slot].trigger(read_pos, rate, velocity, length_samples, attack, release, gain_left, gain_right);
  }

  float sampleFrozenMono(float position) const
  {
    const uint32_t length = freeze_length_ == 0U ? 1U : freeze_length_;
    float pos = position;
    const float length_f = static_cast<float>(length);
    if (pos >= length_f)
    {
      pos -= length_f;
      if (pos >= length_f)
        pos -= length_f;
      if (pos >= length_f)
        pos -= length_f;
    }
    else if (pos < 0.f)
    {
      pos += length_f;
      if (pos < 0.f)
        pos += length_f;
      if (pos < 0.f)
        pos += length_f;
    }

    const uint32_t sampleIndex = static_cast<uint32_t>(pos);
    const float frac = pos - static_cast<float>(sampleIndex);
    uint32_t nextSampleIndex = sampleIndex + 1U;
    if (nextSampleIndex >= length)
      nextSampleIndex = 0U;

    uint32_t a1 = freeze_origin_ + sampleIndex;
    if (a1 >= kMaxCaptureSamples)
      a1 -= kMaxCaptureSamples;
    uint32_t a2 = freeze_origin_ + nextSampleIndex;
    if (a2 >= kMaxCaptureSamples)
      a2 -= kMaxCaptureSamples;

    const float y1 = buf_[a1];
    const float y2 = buf_[a2];
    return y1 + (y2 - y1) * frac;
  }

  void renderGrains(float &left, float &right)
  {
    left = 0.f;
    right = 0.f;

    const float freeze_f = static_cast<float>(freeze_length_ == 0U ? 1U : freeze_length_);

    for (uint32_t grainIndex = 0; grainIndex < kMaxGrains; ++grainIndex)
    {
      Grain &grain = grains_[grainIndex];
      if (!grain.active)
        continue;

      const float mono = sampleFrozenMono(grain.read_pos);
      const float envelope = grainEnvelope(grain.age, grain.length, grain.attack, grain.release);
      const float amp = grain.gain * envelope * mono;
      left += amp * grain.pan_left;
      right += amp * grain.pan_right;

      grain.read_pos += grain.rate;
      if (grain.read_pos >= freeze_f)
      {
        grain.read_pos -= freeze_f;
        if (grain.read_pos >= freeze_f)
          grain.read_pos -= freeze_f;
      }
      else if (grain.read_pos < 0.f)
      {
        grain.read_pos += freeze_f;
        if (grain.read_pos < 0.f)
          grain.read_pos += freeze_f;
      }

      ++grain.age;
      if (grain.age >= grain.length)
        grain.active = false;
    }

    left = softClip(left);
    right = softClip(right);
  }

  float *buf_ = nullptr;

  float fade_norm_ = 0.5f;
  float probability_norm_ = 1.f;
  float mix_ = 1.f;
  float shimmer_norm_ = 1.f;
  float spread_norm_ = 1.f;
  float reverse_norm_ = 0.5f;
  uint8_t length_sel_ = LENGTH_1;
  float bpm_ = 120.f;

  uint32_t write_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint32_t samples_since_unfreeze_ = 0U;
  uint32_t arm_samples_ = 0U;
  uint32_t freeze_origin_ = 0U;
  uint32_t freeze_length_ = kMaxCaptureSamples;
  float captured_peak_ = 0.f;
  bool frozen_ = false;
  bool pad_held_ = false;
  bool arming_ = false;

  float wet_ = 0.f;
  uint32_t tick_counter_ = 0U;
  float internal_tick_phase_ = 0.f;
  bool use_host_clock_ = false;
  uint32_t rng_state_ = 0xC0FFEE01U;
  Grain grains_[kMaxGrains];
};
