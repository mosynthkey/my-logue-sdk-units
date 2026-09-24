#pragma once

/*
 * AcidTouch — NTS-3 genericfx acid voice with an internal 16-step phrase
 * generator. Copyright (C) 2026 acidtouch contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Touch rolls a new pattern (or mutates one). Pad X/Y are cutoff and
 * resonance only. Audio in is replaced; this slot is the oscillator.
 *
 * Phrase grammar follows the public TB-3PO contract (density, accent,
 * slide, octave, 50% gate, accent velocity 118 / normal 72, fixed-time
 * exponential slide, reproducible seed). The voice is a clean-room
 * reimplementation: no GPL upstream sources are copied. See README.md.
 *
 * Pitch uses fastpow2f via fx::noteToHz. Envelope and filter coefficients
 * near 1 use a Taylor exp, not fasterexpf (biased at 0).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class AcidTouch : public Processor
{
public:
  static constexpr uint32_t kSteps = 16U;
  static constexpr float kRootMidi = 36.f;
  static constexpr float kAccentVelocity = 118.f / 127.f;
  static constexpr float kNormalVelocity = 72.f / 127.f;
  static constexpr float kSlideTauSec = 0.055f;
  static constexpr float kGateFraction = 0.5f;
  static constexpr float kMutateChance = 0.25f;
  static constexpr float kHoldMutateSec = 0.45f;
  static constexpr float kAmpAttackSec = 0.003f;
  static constexpr float kAmpReleaseSec = 0.028f;
  static constexpr float kEnvDecaySec = 0.30f;
  static constexpr float kAccentEnvDecaySec = 0.16f;
  static constexpr float kLn2 = 0.69314718f;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    CUT = 0U,
    RES,
    DEN,
    TIME,
    ACID,
    OCT,
    ROOT,
    MODE,
    NUM_PARAMS
  };

  enum Play : uint8_t
  {
    PLAY_GATE = 0U,
    PLAY_LATCH,
    PLAY_THRU,
    PLAY_MUT
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case CUT:
      cutoff_norm_ = param_10bit_to_f32(value);
      break;
    case RES:
      resonance_norm_ = param_10bit_to_f32(value);
      break;
    case DEN:
      density_ = param_10bit_to_f32(value);
      break;
    case TIME:
      time_norm_ = param_10bit_to_f32(value);
      updateStepLength();
      break;
    case ACID:
      acid_ = param_10bit_to_f32(value);
      break;
    case OCT:
      octaves_ = static_cast<uint8_t>(value < 0 ? 0 : (value > 2 ? 2 : value));
      break;
    case ROOT:
      setRoot(value);
      break;
    case MODE:
      applyMode(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    if (index == OCT)
    {
      static const char *kOctaveLabels[] = {"1 OCT", "2 OCT", "3 OCT"};
      if (value < 0)
        value = 0;
      if (value > 2)
        value = 2;
      return kOctaveLabels[value];
    }

    if (index == ROOT)
      return noteLabel(value);

    if (index != MODE)
      return nullptr;

    static const char *kModeLabels[] = {
        "SAW GT", "SQR GT", "SAW LT", "SQR LT", "SAW TH", "SQR TH", "SAW MU", "SQR MU"};
    if (value < 0)
      value = 0;
    if (value > 7)
      value = 7;
    return kModeLabels[value];
  }

  void init(float *) override final
  {
    cutoff_norm_ = 0.5f;
    resonance_norm_ = 0.62f;
    density_ = 0.62f;
    time_norm_ = 0.5f;
    acid_ = 0.48f;
    octaves_ = 1U;
    drive_ = 0.28f;
    root_note_ = 36;
    square_wave_ = false;
    play_ = PLAY_THRU;
    bpm_ = 120.f;
    seed_ = 0xA5C1D001U;
    rng_state_ = seed_;
    finger_down_ = false;
    hold_samples_ = 0U;
    arm_hold_mutate_ = false;
    running_ = false;
    updateStepLength();
    updateGlideCoeff();
    resetVoice();
    generate(seed_);
    running_ = true;
    beginPattern();
  }

  void reset() override final
  {
    finger_down_ = false;
    hold_samples_ = 0U;
    arm_hold_mutate_ = false;
    resetVoice();
    if (play_ == PLAY_THRU || play_ == PLAY_MUT)
    {
      running_ = true;
      beginPattern();
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
    {
      bpm_ = tempo;
      updateStepLength();
    }
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    (void)counter;
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      finger_down_ = false;
      hold_samples_ = 0U;
      arm_hold_mutate_ = false;
      if (play_ == PLAY_GATE)
      {
        running_ = false;
        releaseGate();
      }
      return;
    }

    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      finger_down_ = true;
      return;
    }

    if (phase != k_unit_touch_phase_began || finger_down_)
      return;

    finger_down_ = true;
    hold_samples_ = 0U;
    arm_hold_mutate_ = (play_ == PLAY_LATCH || play_ == PLAY_THRU);
    if (play_ == PLAY_MUT)
      mutate();
    else
      renew();
    running_ = true;
    beginPattern();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;
    const float sample_rate = getSampleRate();
    const float amp_attack = 1.f - expApprox(-1.f / (kAmpAttackSec * sample_rate));
    const float amp_release = 1.f - expApprox(-1.f / (kAmpReleaseSec * sample_rate));
    const float env_decay = expApprox(-1.f / (kEnvDecaySec * sample_rate));
    const float accent_decay = expApprox(-1.f / (kAccentEnvDecaySec * sample_rate));
    const uint32_t hold_mutate_samples =
        static_cast<uint32_t>(kHoldMutateSec * sample_rate);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (arm_hold_mutate_ && finger_down_)
      {
        ++hold_samples_;
        if (hold_samples_ == hold_mutate_samples)
        {
          mutate();
          arm_hold_mutate_ = false;
        }
      }

      if (running_)
        advanceClock();

      const float decay = accent_env_ ? accent_decay : env_decay;
      filter_env_ *= decay;
      if (filter_env_ < 1.0e-4f)
        filter_env_ = 0.f;

      const float amp_coeff = gate_open_ ? amp_attack : amp_release;
      const float amp_target = gate_open_ ? velocity_gain_ : 0.f;
      amp_ += amp_coeff * (amp_target - amp_);

      if (gliding_)
      {
        pitch_ += glide_coeff_ * (pitch_target_ - pitch_);
        const float error = pitch_target_ - pitch_;
        if (error < 0.02f && error > -0.02f)
        {
          pitch_ = pitch_target_;
          gliding_ = false;
        }
      }
      else
      {
        pitch_ = pitch_target_;
      }

      const float wet = renderVoice(sample_rate);
      out[0] = wet;
      out[1] = wet;
      out += 2;
    }
  }

#ifdef ACIDTOUCH_OFFLINE_TEST
  uint32_t debugSeed() const { return seed_; }
  int8_t debugNote(uint32_t step_index) const { return phrase_[step_index].note; }
  bool debugAccent(uint32_t step_index) const { return phrase_[step_index].accent; }
  bool debugSlide(uint32_t step_index) const { return phrase_[step_index].slide; }
  uint32_t debugStepSamples() const { return samples_per_step_; }
  float debugPitch() const { return pitch_; }
  bool debugSlideActive() const { return gliding_; }
  bool debugRunning() const { return running_; }
  float debugBaseHz() const { return cutoffHz(); }
  float debugResK() const { return resonanceK(); }
  Play debugPlay() const { return play_; }

  void debugGenerate(uint32_t seed)
  {
    seed_ = seed;
    generate(seed_);
  }

  void debugRenew() { renew(); }
  void debugMutate() { mutate(); }

  uint32_t debugNoteCount() const
  {
    uint32_t note_count = 0U;
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      if (phrase_[stepIndex].note >= 0)
        ++note_count;
    }
    return note_count;
  }
#endif

private:
  struct Step
  {
    int8_t note;
    bool accent;
    bool slide;
  };

  static float expApprox(float x)
  {
    if (x > -0.35f && x < 0.35f)
    {
      return 1.f + x * (1.f + x * (0.5f + x * (0.16666667f + x * (0.041666668f + x * 0.0083333338f))));
    }
    return fastpow2f(x * 1.44269504f);
  }

  static float clipUnit(float value)
  {
    if (value < 0.f)
      return 0.f;
    if (value > 1.f)
      return 1.f;
    return value;
  }

  static uint32_t nextRandom(uint32_t &state)
  {
    state = state * 1664525U + 1013904223U;
    return state;
  }

  static float randomFloat(uint32_t &state)
  {
    return static_cast<float>(nextRandom(state) >> 8) * (1.f / 16777216.f);
  }

  float timeScale() const
  {
    // Knob center is a synced 1/16. Ends are a 1/32 and an 1/8.
    return fastpow2f(time_norm_ * 2.f - 1.f);
  }

  float accentProbability() const
  {
    return 0.02f + acid_ * 0.75f;
  }

  float slideProbability() const
  {
    return acid_ * acid_ * 0.82f;
  }

  float cutoffHz() const
  {
    const float norm = clipUnit(cutoff_norm_);
    return 80.f * fastpow2f(norm * 6.6443856f);
  }

  float resonanceK() const
  {
    const float y = clipUnit(resonance_norm_);
    return y * y * 3.35f;
  }

  void updateStepLength()
  {
    const float sample_rate = getSampleRate();
    const float sixteenth = sample_rate * 15.f / bpm_;
    float length = sixteenth * timeScale();
    if (length < 64.f)
      length = 64.f;
    samples_per_step_ = static_cast<uint32_t>(length);
    if (step_pos_ >= samples_per_step_)
      step_pos_ = samples_per_step_ - 1U;
  }

  void updateGlideCoeff()
  {
    glide_coeff_ = 1.f - expApprox(-1.f / (kSlideTauSec * getSampleRate()));
  }

  void resetVoice()
  {
    pitch_ = static_cast<float>(root_note_);
    pitch_target_ = static_cast<float>(root_note_);
    phase_ = 0.f;
    gate_open_ = false;
    slide_active_ = false;
    gliding_ = false;
    accent_env_ = false;
    velocity_gain_ = 0.f;
    amp_ = 0.f;
    filter_env_ = 0.f;
    z1_ = 0.f;
    z2_ = 0.f;
    z3_ = 0.f;
    z4_ = 0.f;
    dc_z_ = 0.f;
    step_index_ = 0U;
    step_pos_ = 0U;
    running_ = false;
  }

  void releaseGate()
  {
    gate_open_ = false;
    slide_active_ = false;
    gliding_ = false;
  }

  void applyMode(int32_t value)
  {
    if (value < 0)
      value = 0;
    if (value > 7)
      value = 7;
    square_wave_ = (value & 1) != 0;
    play_ = static_cast<Play>(value >> 1);
    if (play_ == PLAY_GATE && !finger_down_)
    {
      running_ = false;
      releaseGate();
    }
    else if ((play_ == PLAY_THRU || play_ == PLAY_MUT) && !running_)
    {
      running_ = true;
      beginPattern();
    }
  }

  void beginPattern()
  {
    step_index_ = 0U;
    step_pos_ = 0U;
    triggerStep(false);
  }

  void renew()
  {
    seed_ = seed_ * 1664525U + 1013904223U;
    if (seed_ == 0U)
      seed_ = 1U;
    generate(seed_);
  }

  int8_t pickNote(uint32_t &rng) const
  {
    static const int8_t kMinor[] = {0, 2, 3, 5, 7, 8, 10};
    static const uint8_t kWeight[] = {8, 3, 6, 4, 7, 2, 5};
    uint32_t weight_sum = 0U;
    for (uint32_t degreeIndex = 0; degreeIndex < 7U; ++degreeIndex)
      weight_sum += kWeight[degreeIndex];

    uint32_t pick = nextRandom(rng) % weight_sum;
    uint32_t degree_index = 0U;
    for (; degree_index < 6U; ++degree_index)
    {
      if (pick < kWeight[degree_index])
        break;
      pick -= kWeight[degree_index];
    }

    uint32_t octave = 0U;
    const float roll = randomFloat(rng);
    if (octaves_ == 1U)
      octave = roll < 0.72f ? 0U : 1U;
    else if (octaves_ >= 2U)
      octave = roll < 0.55f ? 0U : (roll < 0.85f ? 1U : 2U);

    return static_cast<int8_t>(static_cast<int32_t>(root_note_) + kMinor[degree_index] +
                               static_cast<int32_t>(octave) * 12);
  }

  static const char *noteLabel(int32_t midi_note)
  {
    static char label[8];
    static const char *kPitchClasses[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (midi_note < 0)
      midi_note = 0;
    if (midi_note > 127)
      midi_note = 127;
    const int32_t pitch_class = midi_note % 12;
    const int32_t octave = (midi_note / 12) - 1;
    char *out = label;
    const char *pitch_name = kPitchClasses[pitch_class];
    while (*pitch_name != '\0')
      *out++ = *pitch_name++;
    if (octave < 0)
    {
      *out++ = '-';
      *out++ = static_cast<char>('0' - octave);
    }
    else
    {
      *out++ = static_cast<char>('0' + octave);
    }
    *out = '\0';
    return label;
  }

  void setRoot(int32_t midi_note)
  {
    if (midi_note < 24)
      midi_note = 24;
    if (midi_note > 48)
      midi_note = 48;
    const int32_t delta = midi_note - static_cast<int32_t>(root_note_);
    root_note_ = static_cast<int8_t>(midi_note);
    if (delta == 0)
      return;

    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      if (phrase_[stepIndex].note < 0)
        continue;
      const int32_t shifted = static_cast<int32_t>(phrase_[stepIndex].note) + delta;
      phrase_[stepIndex].note = static_cast<int8_t>(shifted);
    }
    pitch_ += static_cast<float>(delta);
    pitch_target_ += static_cast<float>(delta);
  }

  static void clearStep(Step &step)
  {
    step.note = -1;
    step.accent = false;
    step.slide = false;
  }

  void rollNote(Step &step, uint32_t &rng) const
  {
    if (randomFloat(rng) >= density_)
    {
      clearStep(step);
      return;
    }
    step.note = pickNote(rng);
    step.accent = randomFloat(rng) < accentProbability();
    step.slide = false;
  }

  void ensureNote(uint32_t &rng)
  {
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      if (phrase_[stepIndex].note >= 0)
        return;
    }
    phrase_[0].note = pickNote(rng);
    phrase_[0].accent = true;
    phrase_[0].slide = false;
  }

  void assignSlides(uint32_t &rng)
  {
    const float slide_p = slideProbability();
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % kSteps;
      if (phrase_[stepIndex].note < 0 || phrase_[next_index].note < 0)
      {
        phrase_[stepIndex].slide = false;
        continue;
      }
      phrase_[stepIndex].slide = randomFloat(rng) < slide_p;
    }
  }

  void repairSlides()
  {
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % kSteps;
      if (phrase_[stepIndex].note < 0 || phrase_[next_index].note < 0)
        phrase_[stepIndex].slide = false;
    }
  }

  void generate(uint32_t seed)
  {
    uint32_t rng = seed == 0U ? 1U : seed;
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
      rollNote(phrase_[stepIndex], rng);
    ensureNote(rng);
    assignSlides(rng);
    rng_state_ = rng;
  }

  void mutate()
  {
    uint32_t rng = rng_state_ == 0U ? seed_ : rng_state_;
    for (uint32_t stepIndex = 0; stepIndex < kSteps; ++stepIndex)
    {
      if (randomFloat(rng) >= kMutateChance)
        continue;
      rollNote(phrase_[stepIndex], rng);
      const uint32_t next_index = (stepIndex + 1U) % kSteps;
      if (phrase_[stepIndex].note >= 0 && phrase_[next_index].note >= 0)
        phrase_[stepIndex].slide = randomFloat(rng) < slideProbability();
    }
    ensureNote(rng);
    repairSlides();
    rng_state_ = rng;
  }

  void triggerStep(bool legato)
  {
    const Step &step = phrase_[step_index_];
    if (step.note < 0)
    {
      releaseGate();
      return;
    }

    pitch_target_ = static_cast<float>(step.note);
    if (!legato)
    {
      pitch_ = pitch_target_;
      gliding_ = false;
    }
    else
    {
      gliding_ = true;
    }

    slide_active_ = step.slide;
    if (!legato)
    {
      accent_env_ = step.accent;
      velocity_gain_ = step.accent ? kAccentVelocity : kNormalVelocity;
      filter_env_ = 1.f;
      gate_open_ = true;
    }
    else
    {
      gate_open_ = true;
      if (step.accent)
      {
        accent_env_ = true;
        velocity_gain_ = kAccentVelocity;
      }
    }
  }

  void advanceClock()
  {
    ++step_pos_;
    const uint32_t gate_samples =
        static_cast<uint32_t>(static_cast<float>(samples_per_step_) * kGateFraction);
    if (gate_open_ && !slide_active_ && step_pos_ >= gate_samples)
      gate_open_ = false;

    if (step_pos_ < samples_per_step_)
      return;

    const bool legato = phrase_[step_index_].slide && phrase_[step_index_].note >= 0;
    step_pos_ = 0U;
    step_index_ = (step_index_ + 1U) % kSteps;
    triggerStep(legato);
  }

  static float clampState(float value)
  {
    if (value > 1.6f)
      return 1.6f;
    if (value < -1.6f)
      return -1.6f;
    return value;
  }

  float renderVoice(float sample_rate)
  {
    const float increment = fx::noteToInc(pitch_, sample_rate);
    phase_ += increment;
    if (phase_ >= 1.f)
      phase_ -= 1.f;

    float osc = square_wave_ ? fx::blepPulse(phase_, increment, 0.5f) : fx::blepSaw(phase_, increment);
    osc *= 0.72f;

    const float env_octaves = filter_env_ * (accent_env_ ? 3.4f : 1.55f);
    float hz = cutoffHz() * expApprox(env_octaves * kLn2);
    if (hz < 40.f)
      hz = 40.f;
    if (hz > 14000.f)
      hz = 14000.f;

    const float g = 1.f - expApprox(-6.2831853f * hz / sample_rate);
    float feedback = resonanceK();
    if (accent_env_)
      feedback += 0.18f * filter_env_;
    if (feedback > 3.55f)
      feedback = 3.55f;

    float u = osc - feedback * z4_;
    u = fx::softclip(u);
    z1_ = clampState(z1_ + g * (u - z1_));
    z2_ = clampState(z2_ + g * (z1_ - z2_));
    z3_ = clampState(z3_ + g * (z2_ - z3_));
    z4_ = clampState(z4_ + g * (z3_ - z4_));

    const float drive_gain = 1.f + drive_ * 6.5f;
    const float makeup = 0.42f / (1.f + 0.22f * drive_);
    float shaped = fx::softclip(z4_ * amp_ * drive_gain) * makeup;

    const float dc_coeff = 1.f - expApprox(-6.2831853f * 28.f / sample_rate);
    dc_z_ += dc_coeff * (shaped - dc_z_);
    return shaped - dc_z_;
  }

  Step phrase_[kSteps];
  float cutoff_norm_ = 0.5f;
  float resonance_norm_ = 0.62f;
  float density_ = 0.62f;
  float time_norm_ = 0.5f;
  float acid_ = 0.48f;
  float drive_ = 0.28f;
  float bpm_ = 120.f;
  float pitch_ = 36.f;
  float pitch_target_ = 36.f;
  float phase_ = 0.f;
  float velocity_gain_ = 0.f;
  float amp_ = 0.f;
  float filter_env_ = 0.f;
  float glide_coeff_ = 0.0004f;
  float z1_ = 0.f;
  float z2_ = 0.f;
  float z3_ = 0.f;
  float z4_ = 0.f;
  float dc_z_ = 0.f;
  uint32_t seed_ = 1U;
  uint32_t rng_state_ = 1U;
  uint32_t samples_per_step_ = 6000U;
  uint32_t step_index_ = 0U;
  uint32_t step_pos_ = 0U;
  uint32_t hold_samples_ = 0U;
  uint8_t octaves_ = 1U;
  int8_t root_note_ = 36;
  Play play_ = PLAY_THRU;
  bool square_wave_ = false;
  bool finger_down_ = false;
  bool arm_hold_mutate_ = false;
  bool running_ = false;
  bool gate_open_ = false;
  bool slide_active_ = false;
  bool gliding_ = false;
  bool accent_env_ = false;
};
