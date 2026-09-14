#pragma once

/*
 * File: stoneres.h
 *
 * XY-pad stone grinder with chord resonator bank for NTS-3.
 * Pad rub speed drives fractal-surface friction into a modal stone body;
 * a parallel bandpass bank remaps the body to ROOT/CHORD tones.
 * Host oscillator is muted (instrument slot), like Shaker.
 *
 * Algorithm distilled from stone-grinder (Agarwal / Avanzini style friction
 * excitation + modal body) and Resonator web prototype (chord bandpass bank).
 */

#include "fx_dsp.h"
#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class StoneRes : public Processor
{
public:
  static constexpr uint32_t kRingLen = 2048U;
  static constexpr uint32_t kRingMask = kRingLen - 1U;
  static constexpr uint32_t kTrackCount = 2U;
  static constexpr uint32_t kModalMax = 10U;
  static constexpr uint32_t kChordVoiceMax = 12U;
  static constexpr uint32_t kOctaveRange = 2U;
  static constexpr float kResonatorQ = 42.f;
  static constexpr float kOutputGain = 7.5f;
  static constexpr float kMaxRubSpeed = 1.15f;
  static constexpr float kExciteLimit = 1.8f;
  static constexpr float kHighpassHz = 140.f;

  // 2 tracks * (coarse + fine) * ring
  static constexpr uint32_t kRingFloats = kTrackCount * 2U * kRingLen;

  uint32_t getBufferSize() const override final { return kRingFloats; }

  enum
  {
    LOAD = 0U,
    ROUGH,
    MIX,
    MAT,
    GRAIN,
    DAMP,
    ROOT,
    CHORD,
    NUM_PARAMS
  };

  enum
  {
    MAT_GRANITE = 0,
    MAT_BASALT,
    MAT_MARBLE,
    MAT_SANDSTONE,
    MAT_SLATE,
    MAT_PUMICE,
    MAT_STEEL,
    MAT_CASTIRON,
    MAT_BRONZE,
    NUM_MATERIALS
  };

  enum
  {
    CHORD_MAJ = 0,
    CHORD_MIN,
    CHORD_DOM7,
    CHORD_MAJ7,
    CHORD_MIN7,
    CHORD_SUS4,
    CHORD_DIM,
    CHORD_M7B5,
    NUM_CHORDS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case LOAD:
      load_ = param_10bit_to_f32(value);
      break;
    case ROUGH:
      rough_ = param_10bit_to_f32(value);
      break;
    case MIX:
      mix_ = fx::clip01(value / 1000.f);
      break;
    case MAT:
    {
      uint8_t material = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, NUM_MATERIALS - 1.f));
      if (material != material_)
      {
        material_ = material;
        rebuildModes();
      }
      break;
    }
    case GRAIN:
      grain_ = param_10bit_to_f32(value);
      break;
    case DAMP:
      damp_ = param_10bit_to_f32(value);
      rebuildModes();
      break;
    case ROOT:
    {
      uint8_t root = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, 11.f));
      if (root != root_)
      {
        root_ = root;
        rebuildChord();
      }
      break;
    }
    case CHORD:
    {
      uint8_t chord = static_cast<uint8_t>(fx::clip(static_cast<float>(value), 0.f, NUM_CHORDS - 1.f));
      if (chord != chord_)
      {
        chord_ = chord;
        rebuildChord();
      }
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *kMatNames[NUM_MATERIALS] = {
        "GRANIT", "BASALT", "MARBLE", "SANDST", "SLATE", "PUMICE", "STEEL", "IRON", "BRONZE"};
    static const char *kRootNames[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char *kChordNames[NUM_CHORDS] = {
        "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "DIM", "M7b5"};

    if (index == MAT && value >= 0 && value < NUM_MATERIALS)
      return kMatNames[value];
    if (index == ROOT && value >= 0 && value < 12)
      return kRootNames[value];
    if (index == CHORD && value >= 0 && value < NUM_CHORDS)
      return kChordNames[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    rings_ = allocated_buffer;
    rng_ = 0xC0FFEEu;
    load_ = 0.6f;
    rough_ = 0.5f;
    mix_ = 0.75f;
    grain_ = 0.4f;
    damp_ = 0.4f;
    material_ = MAT_GRANITE;
    root_ = 0;
    chord_ = CHORD_MAJ;
    speed_ = 0.f;
    speed_target_ = 0.f;
    touching_ = false;
    last_x_ = 0.f;
    last_y_ = 0.f;
    pink0_ = pink1_ = pink2_ = hiss_lp_ = 0.f;
    catch_env_ = 0.f;
    body_lp_ = 0.f;
    gate_ = 0.f;
    hp_z_ = 0.f;

    if (rings_ != nullptr)
    {
      for (uint32_t trackIndex = 0; trackIndex < kTrackCount; ++trackIndex)
      {
        float *coarse = ringPtr(trackIndex, 0);
        float *fine = ringPtr(trackIndex, 1);
        fractalRing(coarse, 0.95f, 101U + trackIndex * 7U);
        fractalRing(fine, 0.35f, 501U + trackIndex * 13U);
        track_pos_c_[trackIndex] = static_cast<float>(trackIndex) * (kRingLen / static_cast<float>(kTrackCount));
        track_pos_f_[trackIndex] = static_cast<float>(trackIndex) * (kRingLen / 5.f);
        track_h1_[trackIndex] = 0.f;
        track_h2_[trackIndex] = 0.f;
      }
    }

    clearModes();
    clearChord();
    rebuildModes();
    rebuildChord();
  }

  void teardown() override final { rings_ = nullptr; }

  void reset() override final
  {
    speed_ = 0.f;
    speed_target_ = 0.f;
    touching_ = false;
    catch_env_ = 0.f;
    body_lp_ = 0.f;
    gate_ = 0.f;
    hp_z_ = 0.f;
    pink0_ = pink1_ = pink2_ = hiss_lp_ = 0.f;
    clearModes();
    clearChord();
    for (uint32_t trackIndex = 0; trackIndex < kTrackCount; ++trackIndex)
    {
      track_h1_[trackIndex] = 0.f;
      track_h2_[trackIndex] = 0.f;
    }
  }

  void touchEvent(uint8_t, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);

    if (phase == k_unit_touch_phase_began)
    {
      touching_ = true;
      last_x_ = px;
      last_y_ = py;
      // Small initial scrape so a tap still speaks.
      speed_target_ = 0.35f;
      return;
    }

    if ((phase == k_unit_touch_phase_moved || phase == k_unit_touch_phase_stationary) && touching_)
    {
      const float dx = px - last_x_;
      const float dy = py - last_y_;
      last_x_ = px;
      last_y_ = py;
      const float dist2 = dx * dx + dy * dy;
      float dist = 0.f;
      if (dist2 > 0.f)
        dist = sqrtApprox(dist2);
      // Compress large flicks: linear near zero, soft-knee above ~90px so a
      // corner slam does not dump a click into the modal / chord banks.
      float norm = dist * (1.f / 110.f);
      if (norm > 1.f)
        norm = 1.f + 0.55f * fastlog2f(norm + 1.f);
      if (norm > kMaxRubSpeed)
        norm = kMaxRubSpeed;
      speed_target_ = 0.72f * speed_target_ + 0.28f * norm;
      return;
    }

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      touching_ = false;
      speed_target_ = 0.f;
    }
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;
    const float sr = getSampleRate();
    // Slightly slower speed slew so corner flicks do not impulse the banks.
    const float smooth_k = 1.f - fastexpf(-1.f / (0.014f * sr));
    const float speed_decay = fastexpf(-1.f / (0.12f * sr));
    const float catch_decay = 1.f + (-1.f / (0.05f * sr));
    const float gate_attack = 1.f - fastexpf(-1.f / (0.004f * sr));
    const float gate_release = 1.f - fastexpf(-1.f / (0.08f * sr));
    // Body path tracks mid energy, not sub boom.
    const float body_lp_k = fx::onePoleCoeff(420.f, sr);
    const float hp_k = fx::onePoleCoeff(kHighpassHz, sr);
    const float chord_smooth = 1.f - fastexpf(-1.f / (0.04f * sr));
    const float mix_angle = mix_ * 1.5707963267948966f;
    const float dry_amt = fastcosf(mix_angle);
    // Less wet makeup — chord bank was dominating and muddying.
    const float wet_amt = fastsinf(mix_angle) * 1.55f;
    const float track_radius[kTrackCount] = {0.55f, 1.f};
    // XY maps LOAD+ROUGH; compress when both are high (upper-right).
    const float corner = load_ * rough_;
    const float drive_comp = 1.f / (0.55f + 0.55f * corner);

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      if (!touching_)
        speed_target_ *= speed_decay;
      speed_ += (speed_target_ - speed_) * smooth_k;
      if (speed_ < 0.0005f)
        speed_ = 0.f;

      const float gate_target = touching_ ? 1.f : 0.f;
      gate_ += (gate_target - gate_) * (touching_ ? gate_attack : gate_release);

      float excite = 0.f;
      float catch_impulse = 0.f;
      const float load = load_;
      const float rough = rough_;
      const float sp = speed_;
      const float sp_n = fx::clip01(sp);

      if (sp > 0.002f && rings_ != nullptr)
      {
        catch_env_ *= catch_decay;
        if (catch_env_ < 1e-5f)
          catch_env_ = 0.f;
        const float catch_rate = (sp * (0.35f + 1.8f * grain_)) / sr;
        if (fx::randomFloat(rng_) < catch_rate * load)
        {
          catch_impulse = 0.08f + 0.28f * fx::randomFloat(rng_);
          catch_env_ += catch_impulse;
        }

        const float base_inc = (static_cast<float>(kRingLen) * sp) / sr;
        float h_c0 = 0.f;
        for (uint32_t trackIndex = 0; trackIndex < kTrackCount; ++trackIndex)
        {
          float *coarse = ringPtr(trackIndex, 0);
          float *fine = ringPtr(trackIndex, 1);
          track_pos_c_[trackIndex] += base_inc;
          if (track_pos_c_[trackIndex] >= static_cast<float>(kRingLen))
            track_pos_c_[trackIndex] -= static_cast<float>(kRingLen);
          track_pos_f_[trackIndex] += base_inc * track_radius[trackIndex] * 2.2f;
          if (track_pos_f_[trackIndex] >= static_cast<float>(kRingLen))
            track_pos_f_[trackIndex] -= static_cast<float>(kRingLen);

          const float h_c = readRing(coarse, track_pos_c_[trackIndex]);
          if (trackIndex == 0U)
            h_c0 = h_c;
          float height = readRingSmooth(fine, track_pos_f_[trackIndex]) * (0.3f + rough * 0.7f);
          const float drive = 1.2f + load * 3.5f;
          height = fastertanhf(height * drive) / drive;

          const float d1 = height - track_h1_[trackIndex];
          const float d2 = height - 2.f * track_h1_[trackIndex] + track_h2_[trackIndex];
          track_h2_[trackIndex] = track_h1_[trackIndex];
          track_h1_[trackIndex] = height;

          // Soft-limit derivatives so a fast scan cannot spike.
          const float d1c = fastertanhf(d1 * 5.f) * 0.22f;
          const float d2c = fastertanhf(d2 * 5.f) * 0.22f;
          const float contact = fx::clip(1.f + 0.8f * h_c, 0.f, 2.f);
          excite += (d2c * 38.f + d1c * 5.5f) * contact;
        }
        excite *= load * (0.45f + 0.65f * fx::clip(sp, 0.f, 1.15f));

        // Brighter hiss; cutoff rises with speed.
        const float white = fx::randomFloat(rng_) * 2.f - 1.f;
        pink0_ = 0.997f * pink0_ + 0.029591f * white;
        pink1_ = 0.985f * pink1_ + 0.032534f * white;
        pink2_ = 0.95f * pink2_ + 0.048056f * white;
        const float pink = pink0_ + pink1_ + pink2_ + white * 0.12f;
        const float hiss_k = fx::onePoleCoeff(280.f + 11000.f * sp_n * sp_n, sr);
        hiss_lp_ += (pink - hiss_lp_) * hiss_k;
        excite += hiss_lp_ * 0.14f * hiss_mul_ * (0.35f + 0.65f * rough) * sp_n * load;

        const float grain_rate = sp * grain_ * 0.0028f;
        if (fx::randomFloat(rng_) < grain_rate)
        {
          const float amp = (0.18f + 0.4f * fx::randomFloat(rng_) * fx::randomFloat(rng_)) * load *
                            (0.25f + 0.75f * sp_n);
          const float sign = (fx::randomFloat(rng_) < 0.5f) ? -1.f : 1.f;
          excite += sign * amp * (1.f + 0.35f * h_c0) * impact_mul_;
        }

        if (catch_impulse > 0.f)
        {
          const float sign = (fx::randomFloat(rng_) < 0.5f) ? -1.f : 1.f;
          excite += catch_impulse * load * 1.4f * impact_mul_ * sign;
        }
      }
      else
      {
        catch_env_ *= catch_decay;
      }

      excite *= drive_comp;
      excite = fastertanhf(excite * (1.f / kExciteLimit)) * kExciteLimit;

      body_lp_ += (excite - body_lp_) * body_lp_k;
      float body_in = body_lp_ * 1.6f;
      if (catch_impulse > 0.f)
        body_in += catch_impulse * load * 0.7f;

      const float stone = processModes(excite, body_in);
      // Less raw excite bleed into dry (was dark + clicky).
      const float dry = stone + excite * 0.06f;

      for (uint32_t voiceIndex = 0; voiceIndex < chord_count_; ++voiceIndex)
      {
        chord_b1_[voiceIndex] += (chord_b1_t_[voiceIndex] - chord_b1_[voiceIndex]) * chord_smooth;
        chord_a2_[voiceIndex] += (chord_a2_t_[voiceIndex] - chord_a2_[voiceIndex]) * chord_smooth;
        chord_g_[voiceIndex] += (chord_g_t_[voiceIndex] - chord_g_[voiceIndex]) * chord_smooth;
      }
      const float wet = processChord(dry);

      float mixed = dry * dry_amt + wet * wet_amt;
      // Tilt out sub mud; keep scrape / partials.
      hp_z_ += (mixed - hp_z_) * hp_k;
      mixed = mixed - hp_z_;
      // Soft then hard clip: fastertanhf can overshoot past ±1.
      float sample = fastertanhf(mixed * kOutputGain) * gate_;
      if (sample > 1.f)
        sample = 1.f;
      else if (sample < -1.f)
        sample = -1.f;
      out[0] = sample;
      out[1] = sample;
      out += 2;
    }
  }


private:
  struct Material
  {
    float pitch;
    float t60;
    float ex_add;
    float jitter;
    float slope;
    float hiss;
    float impact;
  };

  static const Material &materialAt(uint8_t index)
  {
    static const Material kMaterials[NUM_MATERIALS] = {
        {1.0f, 1.0f, 0.f, 0.09f, 0.5f, 1.0f, 1.0f},
        {0.82f, 0.85f, 0.06f, 0.07f, 0.75f, 0.8f, 1.1f},
        {1.12f, 1.7f, -0.05f, 0.04f, 0.35f, 0.6f, 0.9f},
        {0.78f, 0.35f, 0.02f, 0.14f, 0.85f, 1.6f, 0.8f},
        {1.45f, 1.35f, 0.18f, 0.06f, 0.3f, 0.7f, 1.2f},
        {1.25f, 0.16f, 0.1f, 0.2f, 0.9f, 1.8f, 0.6f},
        {1.8f, 2.8f, 0.1f, 0.02f, 0.25f, 0.5f, 1.3f},
        {1.3f, 1.6f, 0.05f, 0.05f, 0.45f, 0.7f, 1.2f},
        {1.5f, 3.5f, -0.02f, 0.015f, 0.3f, 0.45f, 1.1f},
    };
    if (index >= NUM_MATERIALS)
      return kMaterials[0];
    return kMaterials[index];
  }

  static const int8_t *chordIntervals(uint8_t chord, uint8_t &count)
  {
    static const int8_t kMaj[] = {0, 4, 7};
    static const int8_t kMin[] = {0, 3, 7};
    static const int8_t kDom7[] = {0, 4, 7, 10};
    static const int8_t kMaj7[] = {0, 4, 7, 11};
    static const int8_t kMin7[] = {0, 3, 7, 10};
    static const int8_t kSus4[] = {0, 5, 7};
    static const int8_t kDim[] = {0, 3, 6};
    static const int8_t kM7b5[] = {0, 3, 6, 10};
    switch (chord)
    {
    case CHORD_MIN:
      count = 3;
      return kMin;
    case CHORD_DOM7:
      count = 4;
      return kDom7;
    case CHORD_MAJ7:
      count = 4;
      return kMaj7;
    case CHORD_MIN7:
      count = 4;
      return kMin7;
    case CHORD_SUS4:
      count = 3;
      return kSus4;
    case CHORD_DIM:
      count = 3;
      return kDim;
    case CHORD_M7B5:
      count = 4;
      return kM7b5;
    case CHORD_MAJ:
    default:
      count = 3;
      return kMaj;
    }
  }

  static float sqrtApprox(float value)
  {
    if (value <= 0.f)
      return 0.f;
    // Quake-style inverse sqrt then invert once.
    float x = value;
    union
    {
      float f;
      uint32_t i;
    } conv;
    conv.f = x;
    conv.i = 0x5f3759dfu - (conv.i >> 1);
    float y = conv.f;
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
  }

  static float radiusFromT60(float t60, float sample_rate)
  {
    const float x = -6.9078f / fx::clip(t60 * sample_rate, 1.f, 1.0e8f);
    // Near-zero: avoid fasterexpf bias (~0.971 at 0).
    if (x > -0.02f)
      return fx::clip(1.f + x + 0.5f * x * x, 0.f, 0.9999f);
    return fx::clip(fastexpf(x), 0.f, 0.9999f);
  }

  float *ringPtr(uint32_t track_index, uint32_t which) const
  {
    return rings_ + (track_index * 2U + which) * kRingLen;
  }

  void fractalRing(float *dst, float hurst, uint32_t seed)
  {
    uint32_t state = seed;
    for (uint32_t sampleIndex = 0; sampleIndex < kRingLen; ++sampleIndex)
      dst[sampleIndex] = 0.f;

    uint32_t step = kRingLen;
    float amp = 1.f;
    while (step > 1U)
    {
      const uint32_t half = step >> 1;
      for (uint32_t sampleIndex = 0; sampleIndex < kRingLen; sampleIndex += step)
      {
        const uint32_t j = (sampleIndex + step) & kRingMask;
        const float mid = 0.5f * (dst[sampleIndex] + dst[j]) + (fx::randomFloat(state) * 2.f - 1.f) * amp;
        dst[(sampleIndex + half) & kRingMask] = mid;
      }
      step = half;
      amp *= fastpow2f(-hurst);
    }

    float peak = 0.f;
    for (uint32_t sampleIndex = 0; sampleIndex < kRingLen; ++sampleIndex)
    {
      const float a = fx::absf(dst[sampleIndex]);
      if (a > peak)
        peak = a;
    }
    if (peak > 0.f)
    {
      const float inv = 1.f / peak;
      for (uint32_t sampleIndex = 0; sampleIndex < kRingLen; ++sampleIndex)
        dst[sampleIndex] *= inv;
    }
  }

  static float readRing(const float *ring, float pos)
  {
    const uint32_t i = static_cast<uint32_t>(pos);
    const float frac = pos - static_cast<float>(i);
    const float x0 = ring[i & kRingMask];
    const float x1 = ring[(i + 1U) & kRingMask];
    return x0 + (x1 - x0) * frac;
  }

  static float readRingSmooth(const float *ring, float pos)
  {
    const uint32_t i = static_cast<uint32_t>(pos);
    const float f = pos - static_cast<float>(i);
    const float xm1 = ring[(i + kRingLen - 1U) & kRingMask];
    const float x0 = ring[i & kRingMask];
    const float x1 = ring[(i + 1U) & kRingMask];
    const float x2 = ring[(i + 2U) & kRingMask];
    const float f2 = f * f;
    const float f3 = f2 * f;
    return (xm1 * (1.f - 3.f * f + 3.f * f2 - f3) + x0 * (4.f - 6.f * f2 + 3.f * f3) +
            x1 * (1.f + 3.f * f + 3.f * f2 - 3.f * f3) + x2 * f3) *
           (1.f / 6.f);
  }

  void clearModes()
  {
    for (uint32_t modeIndex = 0; modeIndex < kModalMax; ++modeIndex)
    {
      modal_y1_[modeIndex] = 0.f;
      modal_y2_[modeIndex] = 0.f;
    }
    body_y1_ = body_y2_ = 0.f;
  }

  void clearChord()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kChordVoiceMax; ++voiceIndex)
    {
      chord_y1_[voiceIndex] = 0.f;
      chord_y2_[voiceIndex] = 0.f;
      chord_g_[voiceIndex] = 0.f;
      chord_g_t_[voiceIndex] = 0.f;
    }
  }

  void rebuildModes()
  {
    const Material &mat = materialAt(material_);
    const float sr = getSampleRate();
    const float size_pitch = 1.f;
    // Raise fundamental: 110 Hz body read as muddy / "low".
    const float f0 = 240.f * mat.pitch * size_pitch;
    const float t60_base = (0.035f + damp_ * damp_ * 0.7f);
    // Slightly denser partials for more mid/high presence.
    const float ex_base = 1.28f + mat.ex_add * 0.85f;
    uint32_t mode_seed = 11U + material_ * 17U;

    modal_count_ = 0;
    for (uint32_t modeIndex = 0; modeIndex < kModalMax; ++modeIndex)
    {
      const float jitter = (fx::randomFloat(mode_seed) - 0.5f) * mat.jitter;
      const float freq = f0 * fastpow2f(fastlog2f(static_cast<float>(modeIndex + 1U)) * ex_base) * (1.f + jitter);
      if (freq > 0.45f * sr)
        break;
      const float t60 = fx::clip(t60_base * mat.t60 * fastpow2f(0.35f * fastlog2f(f0 / freq)), 0.008f, 3.f);
      const float radius = radiusFromT60(t60, sr);
      const float omega = 6.283185307179586f * freq / sr;
      modal_b1_[modal_count_] = 2.f * radius * fastcosf(omega);
      modal_a2_[modal_count_] = radius * radius;
      // Shallower slope + mild high-mode boost vs old dark curve.
      const float tilt = fastpow2f(-(mat.slope * 0.72f) * fastlog2f(freq / f0));
      modal_g_[modal_count_] =
          tilt * (0.55f + 0.7f * fx::randomFloat(mode_seed)) * (1.f - radius);
      ++modal_count_;
    }

    const float body_f = 140.f * size_pitch * sqrtApprox(mat.pitch);
    const float body_t60 = (0.08f + damp_ * 0.55f) * mat.t60;
    const float body_r = radiusFromT60(body_t60, sr);
    const float body_w = 6.283185307179586f * body_f / sr;
    body_b1_ = 2.f * body_r * fastcosf(body_w);
    body_a2_ = body_r * body_r;
    body_g_ = (1.f - body_r) * 0.55f;
    body_gain_ = 0.22f;

    hiss_mul_ = mat.hiss * 1.25f;
    impact_mul_ = mat.impact * 0.85f;
  }

  void rebuildChord()
  {
    uint8_t interval_count = 0;
    const int8_t *intervals = chordIntervals(chord_, interval_count);
    const float sr = getSampleRate();
    const float q = kResonatorQ;
    uint32_t voice_count = 0;

    // Skip the very low octave (-2); keep -1..+2 for clarity.
    for (int32_t octave = -1; octave <= static_cast<int32_t>(kOctaveRange); ++octave)
    {
      for (uint8_t intervalIndex = 0; intervalIndex < interval_count; ++intervalIndex)
      {
        if (voice_count >= kChordVoiceMax)
          break;
        const float midi = 60.f + static_cast<float>(root_) + static_cast<float>(octave * 12) +
                           static_cast<float>(intervals[intervalIndex]);
        if (midi < 40.f || midi > 100.f)
          continue;
        const float freq = fx::noteToHz(midi);
        if (freq > 0.45f * sr)
          continue;
        const float bw = freq / q;
        const float x = -3.141592653589793f * bw / sr;
        const float radius = (x > -0.02f) ? fx::clip(1.f + x, 0.f, 0.9999f) : fx::clip(fastexpf(x), 0.f, 0.9999f);
        const float omega = 6.283185307179586f * freq / sr;
        chord_b1_t_[voice_count] = 2.f * radius * fastcosf(omega);
        chord_a2_t_[voice_count] = radius * radius;
        // Attenuate lower chord voices; lift upper ones slightly.
        float voice_w = 1.f;
        if (octave < 0)
          voice_w = 0.55f;
        else if (octave > 0)
          voice_w = 1.15f;
        chord_g_t_[voice_count] = (1.f - radius) * 1.05f * voice_w;
        ++voice_count;
      }
    }

    for (uint32_t voiceIndex = voice_count; voiceIndex < kChordVoiceMax; ++voiceIndex)
    {
      chord_g_t_[voiceIndex] = 0.f;
      if (voiceIndex >= chord_count_)
      {
        chord_y1_[voiceIndex] = 0.f;
        chord_y2_[voiceIndex] = 0.f;
      }
    }

    chord_count_ = voice_count;
    if (chord_count_ == 0U)
    {
      chord_count_ = 1U;
      chord_b1_t_[0] = 0.f;
      chord_a2_t_[0] = 0.f;
      chord_g_t_[0] = 0.f;
    }

    chord_makeup_ = (1.f / sqrtApprox(static_cast<float>(chord_count_))) * sqrtApprox(q) * 0.55f;
  }

  float processModes(float excite, float body_in)
  {
    float sum = 0.f;
    for (uint32_t modeIndex = 0; modeIndex < modal_count_; ++modeIndex)
    {
      float y = modal_b1_[modeIndex] * modal_y1_[modeIndex] - modal_a2_[modeIndex] * modal_y2_[modeIndex] +
                modal_g_[modeIndex] * excite;
      // Bound resonator state so hard scrapes cannot blow up.
      if (y > 4.f)
        y = 4.f;
      else if (y < -4.f)
        y = -4.f;
      modal_y2_[modeIndex] = modal_y1_[modeIndex];
      modal_y1_[modeIndex] = y;
      sum += y;
    }
    float body = body_b1_ * body_y1_ - body_a2_ * body_y2_ + body_g_ * body_in;
    if (body > 3.f)
      body = 3.f;
    else if (body < -3.f)
      body = -3.f;
    body_y2_ = body_y1_;
    body_y1_ = body;
    return sum * 1.05f + body * body_gain_;
  }

  float processChord(float input)
  {
    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < chord_count_; ++voiceIndex)
    {
      float y = chord_b1_[voiceIndex] * chord_y1_[voiceIndex] - chord_a2_[voiceIndex] * chord_y2_[voiceIndex] +
                chord_g_[voiceIndex] * input;
      if (y > 3.f)
        y = 3.f;
      else if (y < -3.f)
        y = -3.f;
      chord_y2_[voiceIndex] = chord_y1_[voiceIndex];
      chord_y1_[voiceIndex] = y;
      sum += y;
    }
    return sum * chord_makeup_;
  }

  float *rings_ = nullptr;
  float track_pos_c_[kTrackCount] = {};
  float track_pos_f_[kTrackCount] = {};
  float track_h1_[kTrackCount] = {};
  float track_h2_[kTrackCount] = {};

  float modal_b1_[kModalMax] = {};
  float modal_a2_[kModalMax] = {};
  float modal_g_[kModalMax] = {};
  float modal_y1_[kModalMax] = {};
  float modal_y2_[kModalMax] = {};
  uint32_t modal_count_ = 0;

  float body_b1_ = 0.f;
  float body_a2_ = 0.f;
  float body_g_ = 0.f;
  float body_y1_ = 0.f;
  float body_y2_ = 0.f;
  float body_gain_ = 0.f;
  float body_lp_ = 0.f;

  float chord_b1_[kChordVoiceMax] = {};
  float chord_a2_[kChordVoiceMax] = {};
  float chord_g_[kChordVoiceMax] = {};
  float chord_b1_t_[kChordVoiceMax] = {};
  float chord_a2_t_[kChordVoiceMax] = {};
  float chord_g_t_[kChordVoiceMax] = {};
  float chord_y1_[kChordVoiceMax] = {};
  float chord_y2_[kChordVoiceMax] = {};
  uint32_t chord_count_ = 0;
  float chord_makeup_ = 1.f;

  float load_ = 0.6f;
  float rough_ = 0.5f;
  float mix_ = 0.75f;
  float grain_ = 0.4f;
  float damp_ = 0.4f;
  float hiss_mul_ = 1.f;
  float impact_mul_ = 1.f;
  float speed_ = 0.f;
  float speed_target_ = 0.f;
  float catch_env_ = 0.f;
  float pink0_ = 0.f;
  float pink1_ = 0.f;
  float pink2_ = 0.f;
  float hiss_lp_ = 0.f;
  float gate_ = 0.f;
  float hp_z_ = 0.f;
  float last_x_ = 0.f;
  float last_y_ = 0.f;
  uint32_t rng_ = 1U;
  uint8_t material_ = MAT_GRANITE;
  uint8_t root_ = 0;
  uint8_t chord_ = CHORD_MAJ;
  bool touching_ = false;
};
