#pragma once

/*
 * File: tapeosc_mk2.h
 *
 * microKORG2 multi-voice TapeOsc adapter.
 *
 * context->trigger is a bit that the runtime sets high when that voice is
 * triggered. It is not a held key gate, and this API has no note-off.
 * The amp envelope (including Release) is applied after the oscillator, so a
 * long release only fades a full-speed tape. The motor stop cannot be started
 * from the key on microKORG2.
 *
 */

#include "tapeosc_engine.h"
#include "macros.h"
#include "runtime.h"
#include "unit_osc.h"
#include "utils/io_ops.h"
#include <stdint.h>

class TapeOscMk2
{
public:
  typedef TapeOscEngine Voice;

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

    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
    {
      engines_[voiceIndex].setDefaults();
      engines_[voiceIndex].reset();
      engines_[voiceIndex].randomizePhase();
      voice_open_[voiceIndex] = false;
    }

    for (uint8_t paramIndex = 0; paramIndex < Voice::kNumParams; ++paramIndex)
      cached_values_[paramIndex] = static_cast<int32_t>(unit_header.params[paramIndex].init);

    for (uint8_t paramIndex = 0; paramIndex < Voice::kNumParams; ++paramIndex)
      applyToAll(paramIndex, cached_values_[paramIndex]);

    return k_unit_err_none;
  }

  void Teardown() {}

  void Reset()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
    {
      engines_[voiceIndex].reset();
      engines_[voiceIndex].randomizePhase();
      voice_open_[voiceIndex] = false;
    }
  }

  void Resume() { Reset(); }

  void Suspend() {}

  void Process(float *out, uint32_t frames)
  {
    const unit_runtime_osc_context_t *context =
        static_cast<const unit_runtime_osc_context_t *>(runtime_desc_.hooks.runtime_context);

    for (uint32_t voiceIndex = 0; voiceIndex < context->voiceLimit; ++voiceIndex)
    {
      const bool triggered = (context->trigger & (1U << voiceIndex)) != 0;
      if (triggered)
      {
        if (!voice_open_[voiceIndex])
        {
          engines_[voiceIndex].randomizePhase();
          engines_[voiceIndex].beginStart();
          voice_open_[voiceIndex] = true;
        }
      }
      else
      {
        voice_open_[voiceIndex] = false;
      }

      const uint8_t noteWhole = static_cast<uint8_t>(context->pitch[voiceIndex]);
      const float noteFrac = context->pitch[voiceIndex] - static_cast<float>(noteWhole);
      const float w0 = osc_w0f_for_note(noteWhole, static_cast<uint8_t>(noteFrac * 255.f));
      engines_[voiceIndex].setPitch(w0, context->pitch[voiceIndex]);
      ProcessVoice(out, voiceIndex, frames, context);
    }
  }

  void setParameter(uint8_t index, int32_t value)
  {
    if (index >= Voice::kNumParams)
      return;

    cached_values_[index] = value;
    applyToAll(index, value);
  }

  int32_t getParameterValue(uint8_t index) const
  {
    if (index >= Voice::kNumParams)
      return 0;
    return cached_values_[index];
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const
  {
    return engines_[0].parameterString(index, value);
  }

private:
  void applyToAll(uint8_t index, int32_t value)
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kMk2MaxVoices; ++voiceIndex)
      engines_[voiceIndex].applyParam(index, value);
  }

  void ProcessVoice(float *out, uint32_t voiceIndex, uint32_t frames,
                    const unit_runtime_osc_context_t *context)
  {
    const int offset = GetBufferOffset(context, voiceIndex, frames);
    Voice &engine = engines_[voiceIndex];

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      const float mono = engine.render();
      write_oscillator_output_x1(out, mono, offset, context->outputStride, sampleIndex, voiceIndex);
    }
  }

  unit_runtime_desc_t runtime_desc_;
  Voice engines_[kMk2MaxVoices];
  int32_t cached_values_[Voice::kNumParams] = {};
  bool voice_open_[kMk2MaxVoices] = {};
};
