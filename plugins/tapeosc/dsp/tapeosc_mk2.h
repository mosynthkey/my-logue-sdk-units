#pragma once

/*
 * File: tapeosc_mk2.h
 *
 * microKORG2 multi-voice TapeOsc adapter.
 * Each synth voice keeps its own tape. The buffer is int16 so eight voices
 * fit under the 48KB oscillator RAM-load limit.
 *
 * The runtime reports note-on through context->trigger and does not deliver
 * note-off. A trigger spins the motor up; the slot keeps running until the
 * host drops it. Tape stop follows note-off on NTS-1 mkII.
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
  // 1280 samples is about 27 ms. Eight int16 tapes are 20KB.
  static const uint32_t kBufferSize = 1280U;

  typedef TapeOscEngine<kBufferSize, true> Voice;

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
      if (context->trigger & (1U << voiceIndex))
      {
        engines_[voiceIndex].randomizePhase();
        engines_[voiceIndex].beginStart();
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
};
