#pragma once

/*
 * File: ride909.h
 *
 * Tempo-synced TR-909 ride layer for NTS-3.
 * Hold the pad to gate a techno ride wash. On pad-down, the nearest 16th
 * clock becomes relative step 1 (kick / pump). The pattern is a 4-step cycle:
 *   1 = kick sidechain pump, 3 = ride hit (2 and 4 silent).
 * Tap with the kick and rides land on the off-beats automatically.
 * X is 909 Tune: panel pot through R478+VR30 (1/R), zero-order hold,
 * no interpolation. Extremes are asymmetric (−6.1…+9.5 st). Decay shortens
 * as pitch rises, matching the hardware.
 * Y is kick sidechain amount. Depth (MIX) is wet level only; dry input always passes.
 * Edit TONE tilts the first reconstruction pole. Edit DEC adds a soft VCA
 * choke (Roland Cloud–style RC Decay); max = full address envelope (hardware).
 * Edit GAIN boosts the ride above unity (MIX already covers attenuation).
 *
 * Voice path follows the 9090 Ride section of the TR-909 voicing board:
 *   variable clock -> 4040/4520 address -> 6-bit ROM -> resistor DAC
 *   -> address-derived anti-log VCA -> analog reconstruction LPFs
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "ride909_pcm.h"
#include "runtime.h"
#include "tr909_pcm.h"
#include "utils/float_math.h"
#include <stdint.h>

static const float kRide909EnvLut[64] = {
  1.00000000f, 0.95652874f, 0.91494723f, 0.87517332f,
  0.83712843f, 0.80073740f, 0.76592834f, 0.73263247f,
  0.70078401f, 0.67032005f, 0.64118039f, 0.61330747f,
  0.58664622f, 0.56114397f, 0.53675033f, 0.51341712f,
  0.49109823f, 0.46974957f, 0.44932896f, 0.42979607f,
  0.41111229f, 0.39324072f, 0.37614605f, 0.35979451f,
  0.34415379f, 0.32919299f, 0.31488255f, 0.30119421f,
  0.28810092f, 0.27557681f, 0.26359714f, 0.25213824f,
  0.24117747f, 0.23069318f, 0.22066466f, 0.21107209f,
  0.20189652f, 0.19311982f, 0.18472466f, 0.17669445f,
  0.16901332f, 0.16166609f, 0.15463826f, 0.14791594f,
  0.14148585f, 0.13533528f, 0.12945209f, 0.12382464f,
  0.11844183f, 0.11329301f, 0.10836802f, 0.10365713f,
  0.09915102f, 0.09484080f, 0.09071795f, 0.08677433f,
  0.08300214f, 0.07939393f, 0.07594258f, 0.07264126f,
  0.06948345f, 0.06646292f, 0.06357369f, 0.06081006f
};

class Ride909 : public Processor
{
public:
  static constexpr uint32_t kVoiceCount = 4U;
  static constexpr uint32_t kStepsPerCycle = 4U;
  // 9090 Ride clock (schematic): C168=470pF, timing R = R478 + VR30.
  // R478=6.8k, VR30=10kB linear. R477=10k is input protection, not timing.
  // Panel mid (VR=5k) → R=11.8k. Extremes are ASYMMETRIC in pitch:
  //   low  = 11.8/16.8 ≈ −6.12 st,  high = 11.8/6.8 ≈ +9.54 st.
  // X maps the pot through 1/R (not a symmetric semitone bipolar).
  static constexpr float kTuneRFixedOhms = 6800.f;
  static constexpr float kTuneRPotOhms = 10000.f;
  static constexpr float kTuneRMidOhms = kTuneRFixedOhms + 0.5f * kTuneRPotOhms;
  static constexpr float kPitchLowSemitones = -6.12f;
  static constexpr float kPitchHighSemitones = 9.54f;
  static constexpr float kMaxPumpDepth = 0.985f;
  static constexpr float kPumpHoldFraction = 0.32f;
  static constexpr float kPumpReleaseSixteenths = 2.6f;
  static constexpr float kPumpShapeAmount = 0.55f;
  // GAIN Edit: 0 = unity, max ≈ +12 dB (×4) on top of MIX.
  static constexpr float kGainBoostMax = 3.f;
  static constexpr float kVoiceGain = 0.42f;
  static constexpr float kRomPhaseInc = tr909::kRomPhaseInc;
  static constexpr float kLpfACoeff = tr909::kLpfACoeff;
  static constexpr float kLpfBCoeff = tr909::kLpfBCoeff;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    PITCH = 0U,
    PUMP,
    MIX,
    TONE,
    DEC,
    GAIN,
    NUM_PARAMS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case PITCH:
      pitch_norm_ = (static_cast<float>(value) - 512.f) * (1.f / 512.f);
      updateClockRatio();
      break;
    case PUMP:
      pump_amount_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case TONE:
      tone_norm_ = param_10bit_to_f32(value);
      break;
    case DEC:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case GAIN:
      // 1 … 4 (0 dB … ≈ +12 dB). MIX remains the attenuator.
      gain_mul_ = 1.f + param_10bit_to_f32(value) * kGainBoostMax;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    (void)index;
    (void)value;
    return nullptr;
  }

  void init(float *) override final
  {
    pitch_norm_ = 0.f;
    pump_amount_ = 0.f;
    pump_floor_gain_ = 1.f;
    pump_hold_samples_ = 0U;
    pump_gain_ = 1.f;
    mix_ = 1.f;
    tone_norm_ = 0.5f;
    decay_norm_ = 1.f;
    gain_mul_ = 1.f;
    bpm_ = 120.f;
    running_ = false;
    use_host_clock_ = false;
    have_seen_tick_ = false;
    next_step_ = 1U;
    samples_since_tick_ = 0.f;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    updateClockRatio();
    resetVoices();
  }

  void reset() override final
  {
    running_ = false;
    next_step_ = 1U;
    pump_gain_ = 1.f;
    dc_prev_in_ = 0.f;
    dc_prev_out_ = 0.f;
    resetVoices();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 20.f && tempo < 999.f)
      bpm_ = tempo;
  }

  void tempo4ppqnTick(uint32_t counter) override final
  {
    (void)counter;
    use_host_clock_ = true;
    onClockTick();
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;
    (void)y;

    if (phase == k_unit_touch_phase_began || phase == k_unit_touch_phase_moved ||
        phase == k_unit_touch_phase_stationary)
    {
      if (!running_)
      {
        syncToNearestClockAsStep1();
        running_ = true;
      }
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      running_ = false;
      pump_gain_ = 1.f;
      resetVoices();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    const float inv_sr = 1.f / getSampleRate();
    // TONE tilts the first reconstruction pole (darker ↔ brighter).
    const float lpf_a_coeff = fx::clip(kLpfACoeff - 0.18f + tone_norm_ * 0.36f, 0.28f, 0.82f);
    const float decay_tau = decayTauSeconds();

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      samples_since_tick_ += 1.f;
      if (!use_host_clock_)
        advanceInternalClockOneSample();
      advancePumpEnvelope();
      const float a = pump_amount_;
      const float g = pump_gain_;
      // Extra g*g term deepens the duck at high PUMP so max is obvious.
      const float shaped_pump = g * (1.f - a * kPumpShapeAmount + a * kPumpShapeAmount * g * g);
      float wet = renderVoices(lpf_a_coeff, inv_sr, decay_tau) * mix_ * gain_mul_ * shaped_pump;
      if (gain_mul_ > 1.01f)
        wet = fx::softclip(wet);
      out[0] = in[0] + wet;
      out[1] = in[1] + wet;
      in += 2;
      out += 2;
    }
  }

  uint32_t debugNextStep() const { return next_step_; }
  bool debugHaveSeenTick() const { return have_seen_tick_; }
  float debugClockRatio() const { return clock_ratio_; }
  float debugDecayTauSeconds() const { return decayTauSeconds(); }
  float debugGainMul() const { return gain_mul_; }
  float debugPumpFloor() const { return 1.f - pump_amount_ * kMaxPumpDepth; }
  float debugLpfACoeff() const
  {
    return fx::clip(kLpfACoeff - 0.18f + tone_norm_ * 0.36f, 0.28f, 0.82f);
  }

private:
  struct Voice
  {
    bool active = false;
    float rom_phase = 0.f;
    float phase_inc = 0.f;
    float age = 0.f;
    float lpf_a = 0.f;
    float lpf_b = 0.f;
  };

  float samplesPerTick() const
  {
    if (bpm_ <= 0.f)
      return 0.f;
    return getSampleRate() * 60.f / (bpm_ * 4.f);
  }

  // DEC=1 → effectively off (full address envelope). DEC=0 → ~60 ms choke.
  float decayTauSeconds() const
  {
    return 0.060f + decay_norm_ * decay_norm_ * 2.4f;
  }

  static uint8_t readPcm6(uint32_t sample_index)
  {
    return tr909::readPacked6(kRide909PcmPacked, sample_index);
  }

  void triggerPump()
  {
    if (pump_amount_ <= 0.f)
      return;

    pump_floor_gain_ = 1.f - pump_amount_ * kMaxPumpDepth;
    pump_gain_ = pump_floor_gain_;

    if (bpm_ <= 0.f)
      return;

    const float samples_per_16th = samplesPerTick();
    pump_hold_samples_ = static_cast<uint32_t>(samples_per_16th * kPumpHoldFraction);
  }

  void advancePumpEnvelope()
  {
    if (pump_gain_ >= 1.f && pump_hold_samples_ == 0U)
    {
      pump_gain_ = 1.f;
      return;
    }

    if (bpm_ <= 0.f)
      return;

    const float samples_per_16th = samplesPerTick();
    if (samples_per_16th <= 0.f)
      return;

    if (pump_hold_samples_ > 0U)
    {
      --pump_hold_samples_;
      pump_gain_ = pump_floor_gain_;
      return;
    }

    const float release_samples = samples_per_16th * kPumpReleaseSixteenths;
    float step = 3.f / release_samples;
    if (step > 1.f)
      step = 1.f;
    pump_gain_ += (1.f - pump_gain_) * step;
    if (pump_gain_ > 1.f)
      pump_gain_ = 1.f;
  }

  void updateClockRatio()
  {
    // pitch_norm_ −1 = full pot (lowest), +1 = zero pot resistance (highest).
    float r_ohms = kTuneRMidOhms - pitch_norm_ * (0.5f * kTuneRPotOhms);
    if (r_ohms < kTuneRFixedOhms)
      r_ohms = kTuneRFixedOhms;
    if (r_ohms > kTuneRFixedOhms + kTuneRPotOhms)
      r_ohms = kTuneRFixedOhms + kTuneRPotOhms;
    clock_ratio_ = kTuneRMidOhms / r_ohms;
  }

  void resetVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
      voices_[voiceIndex] = Voice{};
    next_voice_index_ = 0U;
  }

  void syncToNearestClockAsStep1()
  {
    const float samples_per_tick = samplesPerTick();
    if (!have_seen_tick_ || samples_per_tick <= 0.f)
    {
      // No grid yet — the next clock pulse becomes step 1.
      next_step_ = 1U;
      return;
    }

    float since = samples_since_tick_;
    if (since > samples_per_tick)
      since = samples_per_tick;
    const float until_next = samples_per_tick - since;

    if (until_next < since)
    {
      // Closer to the upcoming clock → that tick is step 1.
      next_step_ = 1U;
      return;
    }

    // Closer to the previous clock → treat it as step 1 (late kick tap).
    next_step_ = 2U;
    triggerPump();
  }

  void onClockTick()
  {
    samples_since_tick_ = 0.f;
    have_seen_tick_ = true;
    if (!running_)
      return;

    const uint32_t step = next_step_;
    next_step_ = (next_step_ % kStepsPerCycle) + 1U;

    if (step == 1U)
      triggerPump();
    if (step == 3U)
      triggerRide();
  }

  void advanceInternalClockOneSample()
  {
    const float samples_per_tick = samplesPerTick();
    if (samples_per_tick <= 0.f)
      return;

    // samples_since_tick_ was already incremented in process().
    if (samples_since_tick_ >= samples_per_tick)
      onClockTick();
  }

  void triggerRide()
  {
    Voice &voice = voices_[next_voice_index_];
    next_voice_index_ = (next_voice_index_ + 1U) % kVoiceCount;
    voice.active = true;
    voice.rom_phase = 0.f;
    voice.phase_inc = kRomPhaseInc * clock_ratio_;
    voice.age = 0.f;
    voice.lpf_a = 0.f;
    voice.lpf_b = 0.f;
  }

  float renderVoice(Voice &voice, float lpf_a_coeff, float inv_sr, float decay_tau)
  {
    const uint32_t sample_index = static_cast<uint32_t>(voice.rom_phase);
    if (sample_index >= kRide909PcmLength)
    {
      voice.active = false;
      return 0.f;
    }

    const float dac = tr909::dacFromCode(readPcm6(sample_index));
    const float env = kRide909EnvLut[sample_index >> 9];
    // Age-based choke on top of the address envelope (DEC). At max DEC the
    // tau is long vs the ROM, so this stays near 1 for the whole hit.
    const float choke = fasterexpf(-voice.age / decay_tau);
    const float vca = dac * env * choke * kVoiceGain;

    voice.lpf_a += lpf_a_coeff * (vca - voice.lpf_a);
    voice.lpf_b += kLpfBCoeff * (voice.lpf_a - voice.lpf_b);

    voice.rom_phase += voice.phase_inc;
    voice.age += inv_sr;
    if (voice.rom_phase >= static_cast<float>(kRide909PcmLength))
      voice.active = false;

    return voice.lpf_b;
  }

  float renderVoices(float lpf_a_coeff, float inv_sr, float decay_tau)
  {
    float sum = 0.f;

    for (uint32_t voiceIndex = 0; voiceIndex < kVoiceCount; ++voiceIndex)
    {
      Voice &voice = voices_[voiceIndex];
      if (!voice.active)
        continue;

      sum += renderVoice(voice, lpf_a_coeff, inv_sr, decay_tau);
    }

    return tr909::dcBlock(sum, dc_prev_in_, dc_prev_out_);
  }

  Voice voices_[kVoiceCount];
  uint32_t next_voice_index_ = 0U;
  uint32_t next_step_ = 1U;
  float pitch_norm_ = 0.f;
  float clock_ratio_ = 1.f;
  float dc_prev_in_ = 0.f;
  float dc_prev_out_ = 0.f;
  float pump_amount_ = 0.f;
  float pump_floor_gain_ = 1.f;
  float pump_gain_ = 1.f;
  uint32_t pump_hold_samples_ = 0U;
  float mix_ = 1.f;
  float tone_norm_ = 0.5f;
  float decay_norm_ = 1.f;
  float gain_mul_ = 1.f;
  float bpm_ = 120.f;
  float samples_since_tick_ = 0.f;
  bool running_ = false;
  bool use_host_clock_ = false;
  bool have_seen_tick_ = false;
};
