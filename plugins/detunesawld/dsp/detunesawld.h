#pragma once

/*
 * File: detunesawld.h
 *
 * Detuned saw lead. X picks a scale degree inside one octave. Y crossfades
 * the same degree across a 5-octave span (adjacent octaves only). PUMP ducks
 * on each 16th step.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include <stdint.h>

class DetuneSawLd : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 4U;
  static constexpr uint32_t kOctaveSlots = 6U;
  static constexpr float kOctaveSpan = 5.f;
  static constexpr float kDetuneMaxCents = 28.f;
  static constexpr float kPumpDepth = 0.92f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    OCT,
    DETUN,
    SCALE,
    KEY,
    SUB,
    PUMP,
    MIX,
    NUM_PARAMS
  };

  enum ScaleId : uint8_t
  {
    SCALE_IONIAN = 0U,
    SCALE_DORIAN,
    SCALE_PHRYGIAN,
    SCALE_LYDIAN,
    SCALE_MIXOLYDIAN,
    SCALE_AEOLIAN,
    SCALE_LOCRIAN,
    SCALE_MAJPENT,
    SCALE_MINPENT,
    SCALE_CHROMATIC,
    SCALE_COUNT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = param_10bit_to_f32(value);
      break;
    case OCT:
      oct_norm_ = param_10bit_to_f32(value);
      break;
    case DETUN:
      detune_norm_ = param_10bit_to_f32(value);
      break;
    case SCALE:
      scale_id_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(SCALE_COUNT - 1)));
      break;
    case KEY:
      key_note_ = static_cast<int8_t>(fx::clip(static_cast<float>(value), 24.f, 48.f));
      break;
    case SUB:
      sub_norm_ = param_10bit_to_f32(value);
      break;
    case PUMP:
      pump_norm_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *scale_names[SCALE_COUNT] = {
        "IONIAN", "DORIAN", "PHRYG", "LYDIAN", "MIXOLY", "AEOLIA", "LOCRIA", "MAJPNT", "MINPNT", "CHROM"};
    if (index == SCALE && value >= 0 && value < static_cast<int32_t>(SCALE_COUNT))
      return scale_names[value];
    return nullptr;
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    amp_ = 0.f;
    pad_held_ = false;
    use_host_clock_ = false;
    clock_acc_ = 0.f;
    samples_into_step_ = 0.f;
    rng_ = 0xD27E54EDu;
    for (uint32_t octaveIndex = 0; octaveIndex < kOctaveSlots; ++octaveIndex)
    {
      sub_phase_[octaveIndex] = fx::randomFloat(rng_);
      for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
        phase_[octaveIndex][voiceIndex] = fx::randomFloat(rng_);
    }
  }

  void reset() override final
  {
    amp_ = 0.f;
    clock_acc_ = 0.f;
    samples_into_step_ = 0.f;
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t) override final
  {
    use_host_clock_ = true;
    samples_into_step_ = 0.f;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    pad_held_ = phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
                phase == k_unit_touch_phase_stationary;
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    (void)raw;
    const float sample_rate = getSampleRate();
    const float amp_atk = envAlpha(0.008f, sample_rate);
    const float amp_rel = envAlpha(0.09f, sample_rate);
    const float sixteenth = static_cast<float>(fx::samplesPerBeat(bpm_, sample_rate)) * 0.25f;
    const float detune_cents = detune_norm_ * kDetuneMaxCents;
    const float semis = degreeSemitones();
    const float pump_floor = 1.f - pump_norm_ * kPumpDepth;

    float octave_pos = oct_norm_ * kOctaveSpan;
    if (octave_pos > kOctaveSpan)
      octave_pos = kOctaveSpan;
    uint32_t octave_low = static_cast<uint32_t>(octave_pos);
    float octave_frac = octave_pos - static_cast<float>(octave_low);
    if (octave_low >= kOctaveSlots - 1U)
    {
      octave_low = kOctaveSlots - 1U;
      octave_frac = 0.f;
    }
    float gain_low = 0.f;
    float gain_high = 0.f;
    equalPowerGains(octave_frac, gain_low, gain_high);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!use_host_clock_ && sixteenth > 1.f)
      {
        clock_acc_ += 1.f;
        if (clock_acc_ >= sixteenth)
        {
          clock_acc_ -= sixteenth;
          samples_into_step_ = 0.f;
        }
      }

      float step_phase = (sixteenth > 1.f) ? (samples_into_step_ / sixteenth) : 1.f;
      if (step_phase > 1.f)
        step_phase = 1.f;
      // Swell from the duck at the step boundary back to full by the next 16th.
      const float swell = step_phase * step_phase * (3.f - 2.f * step_phase);
      const float pump_gain = pump_floor + (1.f - pump_floor) * swell;
      samples_into_step_ += 1.f;

      float saw = 0.f;
      float sub = 0.f;
      addOctave(octave_low, semis, gain_low, detune_cents, sample_rate, saw, sub);
      if (gain_high > 0.001f)
        addOctave(octave_low + 1U, semis, gain_high, detune_cents, sample_rate, saw, sub);

      const float amp_coeff = pad_held_ ? amp_atk : amp_rel;
      amp_ += ((pad_held_ ? 1.f : 0.f) - amp_) * amp_coeff;

      const float wet = fx::softclip((saw + sub) * amp_ * pump_gain * 1.15f);
      out[0] = fx::mix(in[0], wet, mix_);
      out[1] = fx::mix(in[1], wet * 0.96f + saw * amp_ * pump_gain * 0.04f, mix_);
      in += 2;
      out += 2;
    }
  }

private:
  static float envAlpha(float seconds, float sample_rate)
  {
    const float x = -1.f / (seconds * sample_rate);
    return fx::clip(-x, 0.f, 1.f);
  }

  // sin(x * pi/2) for x in [0, 1], libm-free (Bhaskara on a half turn).
  static float halfTurnSin(float x)
  {
    const float t = x * 0.5f;
    const float u = t * (1.f - t);
    return (16.f * u) / (5.f - 4.f * u);
  }

  static void equalPowerGains(float frac, float &gain_low, float &gain_high)
  {
    gain_low = halfTurnSin(1.f - frac);
    gain_high = halfTurnSin(frac);
  }

  static uint32_t scaleLength(uint8_t scale_id)
  {
    if (scale_id == SCALE_CHROMATIC)
      return 12U;
    if (scale_id == SCALE_MAJPENT || scale_id == SCALE_MINPENT)
      return 5U;
    return 7U;
  }

  static const int8_t *scaleIntervals(uint8_t scale_id)
  {
    static const int8_t kIonian[] = {0, 2, 4, 5, 7, 9, 11};
    static const int8_t kDorian[] = {0, 2, 3, 5, 7, 9, 10};
    static const int8_t kPhrygian[] = {0, 1, 3, 5, 7, 8, 10};
    static const int8_t kLydian[] = {0, 2, 4, 6, 7, 9, 11};
    static const int8_t kMixolydian[] = {0, 2, 4, 5, 7, 9, 10};
    static const int8_t kAeolian[] = {0, 2, 3, 5, 7, 8, 10};
    static const int8_t kLocrian[] = {0, 1, 3, 5, 6, 8, 10};
    static const int8_t kMajPent[] = {0, 2, 4, 7, 9};
    static const int8_t kMinPent[] = {0, 3, 5, 7, 10};
    static const int8_t kChromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

    switch (scale_id)
    {
    case SCALE_DORIAN:
      return kDorian;
    case SCALE_PHRYGIAN:
      return kPhrygian;
    case SCALE_LYDIAN:
      return kLydian;
    case SCALE_MIXOLYDIAN:
      return kMixolydian;
    case SCALE_AEOLIAN:
      return kAeolian;
    case SCALE_LOCRIAN:
      return kLocrian;
    case SCALE_MAJPENT:
      return kMajPent;
    case SCALE_MINPENT:
      return kMinPent;
    case SCALE_CHROMATIC:
      return kChromatic;
    case SCALE_IONIAN:
    default:
      return kIonian;
    }
  }

  float degreeSemitones() const
  {
    const uint32_t length = scaleLength(scale_id_);
    const int8_t *intervals = scaleIntervals(scale_id_);
    uint32_t step = static_cast<uint32_t>(pitch_norm_ * static_cast<float>(length) + 0.5f);
    if (step >= length)
      return 12.f;
    return static_cast<float>(intervals[step]);
  }

  void addOctave(uint32_t octaveIndex, float semis, float gain, float detune_cents, float sample_rate,
                 float &saw_sum, float &sub_sum)
  {
    if (octaveIndex >= kOctaveSlots || gain < 0.001f)
      return;

    static const float kSpread[kVoiceCount] = {-1.f, -0.33f, 0.33f, 1.f};
    const float base_note = static_cast<float>(key_note_) + static_cast<float>(octaveIndex * 12U) + semis;
    float saw = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      const float note = base_note + kSpread[voiceIndex] * detune_cents * (1.f / 100.f);
      const float inc = fx::noteToInc(note, sample_rate);
      phase_[octaveIndex][voiceIndex] = fx::wrap01(phase_[octaveIndex][voiceIndex] + inc);
      saw += fx::blepSaw(phase_[octaveIndex][voiceIndex], inc);
    }
    saw_sum += saw * 0.22f * gain;

    const float sub_inc = fx::noteToInc(base_note - 12.f, sample_rate);
    sub_phase_[octaveIndex] = fx::wrap01(sub_phase_[octaveIndex] + sub_inc);
    sub_sum += fx::blepPulse(sub_phase_[octaveIndex], sub_inc, 0.5f) * sub_norm_ * 0.28f * gain;
  }

  float phase_[kOctaveSlots][kVoiceCount] = {};
  float sub_phase_[kOctaveSlots] = {};
  float amp_ = 0.f;
  float clock_acc_ = 0.f;
  float samples_into_step_ = 0.f;
  float bpm_ = 120.f;
  float pitch_norm_ = 0.f;
  float oct_norm_ = 0.f;
  float detune_norm_ = 0.51f;
  float sub_norm_ = 0.4f;
  float pump_norm_ = 0.45f;
  float mix_ = 1.f;
  uint32_t rng_ = 1U;
  int8_t key_note_ = 36;
  uint8_t scale_id_ = SCALE_IONIAN;
  bool pad_held_ = false;
  bool use_host_clock_ = false;
};
