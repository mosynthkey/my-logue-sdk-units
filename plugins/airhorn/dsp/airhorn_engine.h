#pragma once

/*
 * File: airhorn_engine.h
 *
 * DJ air horn: a 16-bit loop plus a pitch envelope that recreates the opening
 * drop. Settled pitch is measured on the embedded loop (~302.03 Hz ≈ D4 + 49c;
 * see kAirhornSettledHz). Fixed keeps that native pitch; Key alone tracks
 * concert pitch from kAirhornRootMidi.
 *
 * NTS-1 / microKORG2: no natural fade (device EG); PitchMode Fixed or Key.
 * NTS-3: Decay (0-127, 127 = Sustain) replaces Fade; PitchMode Fixed or Pitch
 * with a continuous ±2 oct Pitch parameter (10-bit, smoothed for X-pad play).
 *
 * Sustain uses one wrapping player, not a ping-pong pair. A second player with
 * a runtime crossfade would re-blend the loop tail into a head that already
 * contains that tail, which increases level pumping (~3 dB vs ~0.3 dB on the
 * baked loop). See fade_experiment.py --compare-loop-modes.
 */

#include "airhorn_pcm.h"
#include "utils/float_math.h"
#include <stdint.h>

struct AirHornVoice
{
  bool active = false;
  bool gated = false;
  bool fading = false;
  uint8_t note = 0xFF;
  float pos = 0.f;
  float gain = 1.f;
  float amp = 0.f;
  float pitch_ratio = 1.f;
  float note_transpose = 1.f;
  uint32_t settled_age = 0U;

  static constexpr float kAttackInc = 1.f / (48000.f * 0.004f);
  static constexpr float kReleaseDecayCoeff = 0.99994048f; // tau 0.35 s after note off
  static constexpr float kMinAmp = 0.0005f;
  static constexpr uint32_t kSettledHoldSamples = 12000U; // 250 ms at 48 kHz
  static constexpr float kBaseRate = static_cast<float>(kAirhornSampleRate) / 48000.f;
  static constexpr float kPitchSettled = 0.025f;

  static float pcmToFloat(int16_t sample)
  {
    return static_cast<float>(sample) * (1.f / 32768.f);
  }

  static float hermite(float y0, float y1, float y2, float y3, float frac)
  {
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * frac + c2) * frac + c1) * frac + y1;
  }

  static uint32_t wrapIndex(int32_t index, uint32_t length)
  {
    int32_t wrapped = index % static_cast<int32_t>(length);
    if (wrapped < 0)
      wrapped += static_cast<int32_t>(length);
    return static_cast<uint32_t>(wrapped);
  }

  static float sampleAt(const AirhornSample &horn, float position)
  {
    const int32_t index = static_cast<int32_t>(position);
    const float frac = position - static_cast<float>(index);
    const uint32_t i0 = wrapIndex(index - 1, horn.length);
    const uint32_t i1 = wrapIndex(index, horn.length);
    const uint32_t i2 = wrapIndex(index + 1, horn.length);
    const uint32_t i3 = wrapIndex(index + 2, horn.length);
    const uint32_t offset = horn.offset;
    return hermite(
        pcmToFloat(kAirhornPcm16[offset + i0]),
        pcmToFloat(kAirhornPcm16[offset + i1]),
        pcmToFloat(kAirhornPcm16[offset + i2]),
        pcmToFloat(kAirhornPcm16[offset + i3]),
        frac);
  }

  static float midiTranspose(float midi_note)
  {
    // Concert-pitch tracking from the measured sample root (not assumed D#).
    return fastpow2f((midi_note - kAirhornRootMidi) * (1.f / 12.f));
  }

  bool pitchSettled() const
  {
    return (pitch_ratio > 1.f - kPitchSettled) && (pitch_ratio < 1.f + kPitchSettled);
  }

  void trigger(uint8_t velocity, uint8_t midi_note, float transpose)
  {
    active = true;
    gated = true;
    fading = false;
    note = midi_note;
    pos = 0.f;
    amp = 0.f;
    settled_age = 0U;
    note_transpose = transpose;
    if (note_transpose < 0.25f)
      note_transpose = 0.25f;
    if (note_transpose > 4.f)
      note_transpose = 4.f;
    gain = (static_cast<float>(velocity) + 1.f) * (1.f / 128.f);
    pitch_ratio = kAirhornSamples[0].start_ratio;
    if (pitch_ratio < 0.5f)
      pitch_ratio = 0.5f;
    if (pitch_ratio > 2.f)
      pitch_ratio = 2.f;
  }

  void releaseGate()
  {
    gated = false;
  }

  float render(float natural_decay_coeff, float playback_transpose)
  {
    if (!active)
      return 0.f;

    const AirhornSample &horn = kAirhornSamples[0];

    const bool settled = pitchSettled();
    if (settled)
      ++settled_age;
    else
      settled_age = 0U;

    // Auto-fade only when decay is finite (coeff < 1). Sustain / NTS-1 hold forever
    // while gated and rely on note-off (or the host EG) to release.
    if (gated && natural_decay_coeff < 1.f && settled && settled_age >= kSettledHoldSamples)
      fading = true;

    if (!gated)
      amp *= kReleaseDecayCoeff;
    else if (fading)
      amp *= natural_decay_coeff;
    else
    {
      amp += kAttackInc;
      if (amp > 1.f)
        amp = 1.f;
    }

    if (amp < kMinAmp)
    {
      active = false;
      amp = 0.f;
      return 0.f;
    }

    float transpose = playback_transpose;
    if (transpose < 0.25f)
      transpose = 0.25f;
    if (transpose > 4.f)
      transpose = 4.f;

    float output = sampleAt(horn, pos) * gain * amp;

    pos += kBaseRate * pitch_ratio * transpose;
    const float loop_length = static_cast<float>(horn.length);
    while (pos >= loop_length)
      pos -= loop_length;

    if (horn.env_coeff > 0.f)
      pitch_ratio = 1.f + (pitch_ratio - 1.f) * horn.env_coeff;

    if (output > 1.f)
      output = 1.f;
    if (output < -1.f)
      output = -1.f;

    return output;
  }

  void reset()
  {
    active = false;
    gated = false;
    fading = false;
    amp = 0.f;
    note_transpose = 1.f;
  }
};

class AirHornEngine
{
public:
  static constexpr uint32_t kMaxVoices = 8U;
  static constexpr float kHostSampleRate = 48000.f;
  static constexpr float kPlaybackRate = AirHornVoice::kBaseRate;
  static constexpr float kOutputGain = 0.9f;
  static constexpr int32_t kDecaySustainValue = 127;

  // Shared indices: LEVEL is always 0. Platform headers expose different slots
  // after that (see NTS-1 vs NTS-3 header.c).
  enum
  {
    LEVEL = 0U,
    // NTS-1 / microKORG2
    PMODE = 1U,
    // NTS-3
    DECAY = 1U,
    MIX = 2U,
    PMODE_NTS3 = 3U,
    PITCH = 4U,
  };

  enum PitchMode
  {
    kPitchFixed = 0,
    kPitchTrack = 1, // Key (keyboard) or Pitch (param), per tracking source
  };

  void init()
  {
    level_ = 1.f;
    natural_decay_coeff_ = 1.f; // default: Sustain / no auto-fade (NTS-1)
    mix_ = 1.f;
    pitch_mode_ = kPitchFixed;
    pitch_transpose_target_ = 1.f;
    pitch_transpose_ = 1.f;
    track_from_param_ = false;
    next_voice_ = 0U;
    clearVoices();
  }

  void reset() { clearVoices(); }

  // NTS-3: pitch follows the Pitch parameter. NTS-1/mk2: pitch follows MIDI note.
  void setTrackFromParam(bool enabled) { track_from_param_ = enabled; }

  void setParameter(uint8_t index, int32_t value)
  {
    if (track_from_param_)
    {
      switch (index)
      {
      case LEVEL:
        level_ = param10BitToFloat(value);
        break;
      case DECAY:
        natural_decay_coeff_ = decayParamToCoeff(value);
        break;
      case MIX:
        mix_ = value / 1000.f;
        if (mix_ < 0.f)
          mix_ = 0.f;
        if (mix_ > 1.f)
          mix_ = 1.f;
        break;
      case PMODE_NTS3:
        pitch_mode_ = (value != 0) ? kPitchTrack : kPitchFixed;
        break;
      case PITCH:
        // Continuous ±2 oct: 0 → -24st, 512 → 0, 1023 → +24st.
        pitch_transpose_target_ = pitchParamToTranspose(value);
        break;
      default:
        break;
      }
      return;
    }

    switch (index)
    {
    case LEVEL:
      // NTS-1 / microKORG2: 0-127
      level_ = (value <= 0) ? 0.f : (value >= 127 ? 1.f : value * (1.f / 127.f));
      break;
    case PMODE:
      pitch_mode_ = (value != 0) ? kPitchTrack : kPitchFixed;
      break;
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    if (track_from_param_)
    {
      if (index == DECAY)
      {
        if (value >= kDecaySustainValue)
          return "Sustain";
        static char decay_label[8];
        if (value < 0)
          value = 0;
        if (value > 126)
          value = 126;
        // 0-126 shown as integers; 127 is Sustain above.
        decay_label[0] = static_cast<char>('0' + (value / 100));
        decay_label[1] = static_cast<char>('0' + ((value / 10) % 10));
        decay_label[2] = static_cast<char>('0' + (value % 10));
        decay_label[3] = '\0';
        // Strip leading zeros for a short display (keep a single 0).
        const char *label = decay_label;
        while (label[0] == '0' && label[1] != '\0')
          ++label;
        return label;
      }
      if (index == PMODE_NTS3)
        return (value != 0) ? "Pitch" : "Fixed";
      return nullptr;
    }

    if (index == PMODE)
      return (value != 0) ? "Key" : "Fixed";
    return nullptr;
  }

  void startVoice(uint8_t velocity, uint8_t note)
  {
    float transpose = 1.f;
    if (pitch_mode_ == kPitchTrack && !track_from_param_)
      transpose = AirHornVoice::midiTranspose(static_cast<float>(note));
    voices_[next_voice_].trigger(velocity, note, transpose);
    next_voice_ = (next_voice_ + 1U) % kMaxVoices;
  }

  void releaseNote(uint8_t note)
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
    {
      if (voices_[voiceIndex].active && voices_[voiceIndex].note == note)
        voices_[voiceIndex].releaseGate();
    }
  }

  void releaseAll()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex].releaseGate();
  }

  float renderMono() const
  {
    // One-pole toward the Pitch target (~8 ms) so X-pad sweeps stay continuous.
    pitch_transpose_ += (pitch_transpose_target_ - pitch_transpose_) * kPitchSmoothCoeff;

    float wet = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      wet += voices_[voiceIndex].render(natural_decay_coeff_, voicePlaybackTranspose(voiceIndex));
    return wet;
  }

  float outputLevel() const { return level_ * kOutputGain; }

  float naturalDecayCoeff() const { return natural_decay_coeff_; }

  float mix() const { return mix_; }

  PitchMode pitchMode() const { return pitch_mode_; }

  bool trackFromParam() const { return track_from_param_; }

  float pitchTranspose() const { return pitch_transpose_; }

  float voicePlaybackTranspose(uint32_t voiceIndex) const
  {
    if (pitch_mode_ == kPitchFixed)
      return 1.f;
    if (track_from_param_)
      return pitch_transpose_;
    return voices_[voiceIndex].note_transpose;
  }

  // Direct voice access for microKORG2 (per-voice rendering outside the pool).
  static float noteTransposeFor(float midi_note)
  {
    return AirHornVoice::midiTranspose(midi_note);
  }

private:
  static constexpr float kDecayTauMinSec = 0.15f;
  static constexpr float kDecayTauMaxSec = 8.f;
  static constexpr float kPitchSmoothCoeff = 0.0026f; // ≈ 1 - e^(-1/(0.008*48000))

  static float param10BitToFloat(int32_t value)
  {
    return static_cast<uint16_t>(value) * 9.77517106549365e-004f;
  }

  static float pitchParamToTranspose(int32_t value)
  {
    // 0..1023 → -24..+24 semitones continuously (center 512 = unison).
    float norm = (static_cast<float>(value) - 512.f) * (1.f / 512.f);
    if (norm < -1.f)
      norm = -1.f;
    if (norm > 1.f)
      norm = 1.f;
    return fastpow2f(norm * 2.f);
  }

  static float decayParamToCoeff(int32_t value)
  {
    if (value >= kDecaySustainValue)
      return 1.f;
    if (value < 0)
      value = 0;

    // 0 → short fade, 126 → long fade. Use fastexpf; |x| is tiny near Sustain
    // but we never take that path at 127.
    const float norm = static_cast<float>(value) * (1.f / 126.f);
    const float tau = kDecayTauMinSec * fastpow2f(norm * 5.807f); // ≈ log2(8/0.15)
    const float x = -1.f / (tau * kHostSampleRate);
    // Linearization is accurate for |x| ≪ 1 and avoids fasterexpf bias at 0.
    if (x > -0.0025f)
      return 1.f + x;
    return fastexpf(x);
  }

  void clearVoices()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMaxVoices; ++voiceIndex)
      voices_[voiceIndex].reset();
  }

  uint8_t next_voice_ = 0U;
  float level_ = 1.f;
  float natural_decay_coeff_ = 1.f;
  float mix_ = 1.f;
  PitchMode pitch_mode_ = kPitchFixed;
  float pitch_transpose_target_ = 1.f;
  mutable float pitch_transpose_ = 1.f;
  bool track_from_param_ = false;
  mutable AirHornVoice voices_[kMaxVoices];
};
