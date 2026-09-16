#pragma once

/*
 * File: steprndflt.h
 *
 * Tempo-synced sample-and-hold LFO into a multimode resonant TPT SVF. Dry by
 * default; touch engages. Each grid period redraws a random bipolar offset
 * around CUT; DEPTH scales that offset in octaves. Y is resonance (capped).
 * TYPE picks Peak / LPF12 / LPF24 / BPF / HPF12 / HPF24 / Variable. Variable
 * morphs continuously across LPF24→LPF12→BPF→HPF12→HPF24 (adjacent taps mixed
 * by fraction) and redraws that internal SVF-type position each step (slewed).
 * LEVEL scales wet before the final softclip.
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StepRndFlt : public Processor
{
public:
  static constexpr float kMinFilterCutoffHz = 40.f;
  static constexpr float kMaxFilterCutoffHz = 18000.f;
  static constexpr float kMaxDepthOctaves = 5.f;
  static constexpr float kMaxResonanceNorm = 0.8f;
  static constexpr float kParamSmoothCoeff = 0.0025f;
  static constexpr float kMinSlewSec = 0.0005f;
  static constexpr float kMaxSlewSec = 0.12f;
  static constexpr uint8_t kNumPeriods = 8U;
  static constexpr uint8_t kNumTypes = 7U;
  static constexpr uint8_t kNumSvfMorphTaps = 5U;

  uint32_t getBufferSize() const override final { return 0; }

  enum
  {
    DEPTH = 0U,
    RES,
    MIX,
    CUT,
    STEPS,
    TYPE,
    SLEW,
    LEVEL,
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
    TYPE_PEAK = 0U,
    TYPE_LPF12,
    TYPE_LPF24,
    TYPE_BPF,
    TYPE_HPF12,
    TYPE_HPF24,
    TYPE_VARIABLE
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case DEPTH:
      depth_target_ = param_10bit_to_f32(value);
      break;
    case RES:
      resonance_norm_target_ = param_10bit_to_f32(value) * kMaxResonanceNorm;
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case CUT:
      cutoff_norm_target_ = param_10bit_to_f32(value);
      break;
    case STEPS:
      period_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumPeriods - 1U)));
      break;
    case TYPE:
      type_sel_ = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, static_cast<float>(kNumTypes - 1U)));
      break;
    case SLEW:
      slew_norm_ = param_10bit_to_f32(value);
      break;
    case LEVEL:
      level_ = param_10bit_to_f32(value);
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *period_names[kNumPeriods] = {"4Bar", "2Bar", "16St", "8St", "4St", "2St", "1St", "1/2"};
    static const char *type_names[kNumTypes] = {"Peak", "LP12", "LP24", "BPF", "HP12", "HP24", "Var"};
    if (index == STEPS && value >= 0 && value < static_cast<int32_t>(kNumPeriods))
      return period_names[value];
    if (index == TYPE && value >= 0 && value < static_cast<int32_t>(kNumTypes))
      return type_names[value];
    return nullptr;
  }

  void init(float *) override final
  {
    bpm_ = 120.f;
    depth_target_ = 0.55f;
    depth_smooth_ = 0.55f;
    resonance_norm_target_ = 0.45f * kMaxResonanceNorm;
    resonance_norm_smooth_ = 0.45f * kMaxResonanceNorm;
    cutoff_norm_target_ = 0.5f;
    cutoff_norm_smooth_ = 0.5f;
    slew_norm_ = 0.15f;
    mix_ = 1.f;
    level_ = 1.f;
    period_sel_ = PERIOD_1STEP;
    type_sel_ = TYPE_PEAK;
    clock_acc_ = 0.f;
    hold_bipolar_ = 0.f;
    hold_svf_type_ = 0.5f;
    svf_type_smooth_ = 0.5f;
    cutoff_hz_smooth_ = baseCutoffHz(0.5f);
    rng_ = 0xA5F15237U;
    pad_held_ = false;
    resetSvf();
  }

  void reset() override final
  {
    depth_smooth_ = depth_target_;
    resonance_norm_smooth_ = resonance_norm_target_;
    cutoff_norm_smooth_ = cutoff_norm_target_;
    clock_acc_ = 0.f;
    hold_bipolar_ = 0.f;
    svf_type_smooth_ = hold_svf_type_;
    cutoff_hz_smooth_ = baseCutoffHz(cutoff_norm_smooth_);
    pad_held_ = false;
    resetSvf();
  }

  void setTempo(float tempo) override final
  {
    if (tempo > 40.f && tempo < 300.f)
      bpm_ = tempo;
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t, uint32_t) override final
  {
    if (phase == k_unit_touch_phase_began)
    {
      pad_held_ = true;
      clock_acc_ = 0.f;
      resetSvf();
      sampleHold();
      svf_type_smooth_ = hold_svf_type_;
      return;
    }
    if (phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary)
    {
      pad_held_ = true;
      return;
    }
    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      resetSvf();
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    const float sr = getSampleRate();
    const float beat = static_cast<float>(fx::samplesPerBeat(bpm_, sr));
    const float period_samples = beat * 0.25f * periodSixteenths(period_sel_);
    const float slew_sec = kMinSlewSec + slew_norm_ * slew_norm_ * (kMaxSlewSec - kMinSlewSec);
    // Per-sample coeff near 1: linearize exp (fasterexpf is biased near 0).
    const float slew_x = -1.f / (slew_sec * sr);
    const float slew_coeff = fx::clip(1.f + slew_x, 0.f, 1.f);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = 0.f;
      float live_right = 0.f;
      fx::pickLive(in, raw, live_left, live_right);

      if (!pad_held_)
      {
        out[0] = live_left;
        out[1] = live_right;
        in += 2;
        if (raw != nullptr)
          raw += 2;
        out += 2;
        continue;
      }

      depth_smooth_ += (depth_target_ - depth_smooth_) * kParamSmoothCoeff;
      resonance_norm_smooth_ += (resonance_norm_target_ - resonance_norm_smooth_) * kParamSmoothCoeff;
      cutoff_norm_smooth_ += (cutoff_norm_target_ - cutoff_norm_smooth_) * kParamSmoothCoeff;

      clock_acc_ += 1.f;
      if (clock_acc_ >= period_samples)
      {
        clock_acc_ -= period_samples;
        sampleHold();
      }

      const float target_hz = modulatedCutoffHz(cutoff_norm_smooth_, depth_smooth_, hold_bipolar_);
      cutoff_hz_smooth_ += (target_hz - cutoff_hz_smooth_) * slew_coeff;
      svf_type_smooth_ += (hold_svf_type_ - svf_type_smooth_) * slew_coeff;

      float filtered_left = 0.f;
      float filtered_right = 0.f;
      if (type_sel_ == TYPE_VARIABLE)
      {
        filtered_left = processVariableFilter(live_left, cutoff_hz_smooth_, resonance_norm_smooth_, svf_type_smooth_,
                                              svf_left_a_, svf_left_b_, svf_left_c_);
        filtered_right = processVariableFilter(live_right, cutoff_hz_smooth_, resonance_norm_smooth_, svf_type_smooth_,
                                               svf_right_a_, svf_right_b_, svf_right_c_);
      }
      else
      {
        filtered_left = processFilter(live_left, cutoff_hz_smooth_, resonance_norm_smooth_, type_sel_, svf_left_a_, svf_left_b_);
        filtered_right = processFilter(live_right, cutoff_hz_smooth_, resonance_norm_smooth_, type_sel_, svf_right_a_, svf_right_b_);
      }

      // Clip before softclip: fastertanhf is unusable for |x| ≫ 1.
      const float wet_left = fx::softclip(fx::clip(filtered_left * level_, -1.5f, 1.5f));
      const float wet_right = fx::softclip(fx::clip(filtered_right * level_, -1.5f, 1.5f));

      out[0] = fx::mix(live_left, wet_left, mix_);
      out[1] = fx::mix(live_right, wet_right, mix_);
      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

private:
  struct SvfState
  {
    float ic1 = 0.f;
    float ic2 = 0.f;
  };

  void resetSvf()
  {
    svf_left_a_ = SvfState();
    svf_left_b_ = SvfState();
    svf_left_c_ = SvfState();
    svf_right_a_ = SvfState();
    svf_right_b_ = SvfState();
    svf_right_c_ = SvfState();
  }

  void sampleHold()
  {
    hold_bipolar_ = fx::randomFloat(rng_) * 2.f - 1.f;
    if (type_sel_ == TYPE_VARIABLE)
      hold_svf_type_ = fx::randomFloat(rng_);
  }

  static float periodSixteenths(uint8_t period_sel)
  {
    static const float kPeriods[kNumPeriods] = {64.f, 32.f, 16.f, 8.f, 4.f, 2.f, 1.f, 0.5f};
    return kPeriods[period_sel < kNumPeriods ? period_sel : PERIOD_1STEP];
  }

  static float baseCutoffHz(float cutoff_norm)
  {
    return 70.f * fasterpow2f(cutoff_norm * 8.f);
  }

  static float modulatedCutoffHz(float cutoff_norm, float depth, float hold_bipolar)
  {
    const float base_hz = baseCutoffHz(cutoff_norm);
    const float octaves = depth * kMaxDepthOctaves * hold_bipolar;
    return fx::clip(base_hz * fasterpow2f(octaves), kMinFilterCutoffHz, kMaxFilterCutoffHz);
  }

  static float resonanceQ(float resonance_norm)
  {
    return 0.7f + resonance_norm * resonance_norm * 14.f;
  }

  static float resonanceComp(float resonance_norm)
  {
    return 1.f / (1.f + resonance_norm * resonance_norm * 3.5f);
  }

  // Continuous SVF type in [0,1]: LPF24 → LPF12 → BPF → HPF12 → HPF24.
  static float morphSvfType(float svf_type_norm, float lp24, float lp12, float bpf, float hp12, float hp24)
  {
    const float taps[kNumSvfMorphTaps] = {lp24, lp12, bpf, hp12, hp24};
    const float pos = fx::clip01(svf_type_norm) * static_cast<float>(kNumSvfMorphTaps - 1U);
    const uint8_t tap_index = static_cast<uint8_t>(pos);
    if (tap_index >= kNumSvfMorphTaps - 1U)
      return taps[kNumSvfMorphTaps - 1U];
    const float frac = pos - static_cast<float>(tap_index);
    return taps[tap_index] + (taps[tap_index + 1U] - taps[tap_index]) * frac;
  }

  // TPT SVF tick: returns low / band / high.
  static void tickSvf(float input, float g, float k, float drive_comp, SvfState &state, float &low, float &band, float &high)
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

  float processFilter(float input, float cutoff_hz, float resonance_norm, uint8_t type, SvfState &stage_a, SvfState &stage_b)
  {
    const float fc = fx::clip(cutoff_hz, kMinFilterCutoffHz, 16000.f);
    const float g = fastertanfullf(3.14159265f * fc / getSampleRate());
    const float k = 1.f / resonanceQ(resonance_norm);

    float low = 0.f;
    float band = 0.f;
    float high = 0.f;

    if (type == TYPE_PEAK)
    {
      // Peaking/bell: flat dry plus resonant band boost at fc (no drive atten).
      tickSvf(input, g, k, 1.f, stage_a, low, band, high);
      return input + band * 2.f;
    }

    const float drive_comp = resonanceComp(resonance_norm);
    tickSvf(input, g, k, drive_comp, stage_a, low, band, high);

    float output = low;
    switch (type)
    {
    case TYPE_LPF24:
    {
      float low2 = 0.f;
      float band2 = 0.f;
      float high2 = 0.f;
      // Second stage slightly less resonant so 24 dB stays stable.
      tickSvf(low, g, k * 1.35f, 1.f, stage_b, low2, band2, high2);
      output = low2;
      break;
    }
    case TYPE_BPF:
      output = band;
      break;
    case TYPE_HPF12:
      output = high;
      break;
    case TYPE_HPF24:
    {
      float low2 = 0.f;
      float band2 = 0.f;
      float high2 = 0.f;
      tickSvf(high, g, k * 1.35f, 1.f, stage_b, low2, band2, high2);
      output = high2;
      break;
    }
    case TYPE_LPF12:
    default:
      output = low;
      break;
    }

    return output;
  }

  float processVariableFilter(float input, float cutoff_hz, float resonance_norm, float svf_type_norm,
                              SvfState &stage_a, SvfState &stage_lp24, SvfState &stage_hp24)
  {
    const float fc = fx::clip(cutoff_hz, kMinFilterCutoffHz, 16000.f);
    const float g = fastertanfullf(3.14159265f * fc / getSampleRate());
    const float k = 1.f / resonanceQ(resonance_norm);
    const float drive_comp = resonanceComp(resonance_norm);

    float lp12 = 0.f;
    float bpf = 0.f;
    float hp12 = 0.f;
    tickSvf(input, g, k, drive_comp, stage_a, lp12, bpf, hp12);

    float lp24 = 0.f;
    float band_lp = 0.f;
    float high_lp = 0.f;
    tickSvf(lp12, g, k * 1.35f, 1.f, stage_lp24, lp24, band_lp, high_lp);

    float hp24 = 0.f;
    float band_hp = 0.f;
    float high_hp = 0.f;
    tickSvf(hp12, g, k * 1.35f, 1.f, stage_hp24, hp24, band_hp, high_hp);

    return morphSvfType(svf_type_norm, lp24, lp12, bpf, hp12, hp24);
  }

  float clock_acc_ = 0.f;
  float hold_bipolar_ = 0.f;
  float hold_svf_type_ = 0.5f;
  float svf_type_smooth_ = 0.5f;
  float cutoff_hz_smooth_ = 1000.f;
  float bpm_ = 120.f;
  float depth_target_ = 0.55f;
  float depth_smooth_ = 0.55f;
  float resonance_norm_target_ = 0.45f * kMaxResonanceNorm;
  float resonance_norm_smooth_ = 0.45f * kMaxResonanceNorm;
  float cutoff_norm_target_ = 0.5f;
  float cutoff_norm_smooth_ = 0.5f;
  float slew_norm_ = 0.15f;
  float mix_ = 1.f;
  float level_ = 1.f;
  uint32_t rng_ = 0xA5F15237U;
  uint8_t period_sel_ = PERIOD_1STEP;
  uint8_t type_sel_ = TYPE_PEAK;
  bool pad_held_ = false;
  SvfState svf_left_a_;
  SvfState svf_left_b_;
  SvfState svf_left_c_;
  SvfState svf_right_a_;
  SvfState svf_right_b_;
  SvfState svf_right_c_;
};
