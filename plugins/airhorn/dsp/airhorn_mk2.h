#pragma once

/*
 * File: airhorn_mk2.h
 *
 * microKORG2 multi-voice AirHorn oscillator adapter.
 * PitchMode Fixed: measured sample pitch. Key: keyboard tracks concert pitch
 * from kAirhornRootMidi.
 */

#include "airhorn_engine.h"
#include "macros.h"
#include "runtime.h"
#include "unit_osc.h"
#include "utils/io_ops.h"
#include <stdint.h>

class AirHornMk2
{
public:
  enum
  {
    kLevel = 0U,
    kPitchMode = 1U,
    kNumParams
  };

  int8_t Init(const unit_runtime_desc_t *desc)
  {
    if (!desc)
      return k_unit_err_undef;

    if (desc->target != unit_header.target)
      return k_unit_err_target;

    if (!UNIT_API_IS_COMPAT(desc->api))
      return k_unit_err_api_version;

    if (desc->samplerate != 48000)
      return k_unit_err_samplerate;

    runtime_desc_ = *desc;
    engine_.init();
    engine_.setTrackFromParam(false);

    for (uint8_t paramIndex = 0; paramIndex < kNumParams; ++paramIndex)
      cached_values_[paramIndex] = static_cast<int32_t>(unit_header.params[paramIndex].init);

    for (uint8_t paramIndex = 0; paramIndex < kNumParams; ++paramIndex)
      engine_.setParameter(paramIndex, cached_values_[paramIndex]);

    return k_unit_err_none;
  }

  void Teardown() {}

  void Reset()
  {
    engine_.reset();
    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
      voices_[voiceIndex].reset();
  }

  void Resume() { Reset(); }

  void Suspend() {}

  void Process(float *out, uint32_t frames)
  {
    const unit_runtime_osc_context_t *context =
        static_cast<const unit_runtime_osc_context_t *>(runtime_desc_.hooks.runtime_context);

    for (uint32_t voiceIndex = 0; voiceIndex < context->voiceLimit; ++voiceIndex)
    {
      if (context->trigger & (1U << voiceIndex))
      {
        const float midi_note = context->pitch[voiceIndex];
        const uint8_t note = static_cast<uint8_t>(midi_note);
        float transpose = 1.f;
        if (engine_.pitchMode() == AirHornEngine::kPitchTrack)
          transpose = AirHornEngine::noteTransposeFor(midi_note);
        voices_[voiceIndex].trigger(127, note, transpose);
      }

      ProcessVoice(out, voiceIndex, frames, context);
    }
  }

  void setParameter(uint8_t index, int32_t value)
  {
    if (index >= kNumParams)
      return;

    cached_values_[index] = value;
    engine_.setParameter(index, value);
  }

  int32_t getParameterValue(uint8_t index) const
  {
    if (index >= kNumParams)
      return 0;
    return cached_values_[index];
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    return engine_.getParameterStrValue(index, value);
  }

private:
  void ProcessVoice(float *out, uint32_t voiceIndex, uint32_t frames,
                    const unit_runtime_osc_context_t *context)
  {
    const int offset = GetBufferOffset(context, voiceIndex, frames);
    const float transpose = engine_.pitchMode() == AirHornEngine::kPitchTrack
                                ? voices_[voiceIndex].note_transpose
                                : 1.f;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float mono =
          voices_[voiceIndex].render(engine_.naturalDecayCoeff(), transpose) * engine_.outputLevel();
      write_oscillator_output_x1(out, mono, offset, context->outputStride, sampleIndex, voiceIndex);
    }
  }

  unit_runtime_desc_t runtime_desc_;
  AirHornEngine engine_;
  AirHornVoice voices_[kMk2MaxVoices];
  int32_t cached_values_[kNumParams] = {};
};
