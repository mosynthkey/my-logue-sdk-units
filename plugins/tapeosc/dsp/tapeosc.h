#pragma once

/*
 * File: tapeosc.h
 *
 * NTS-1 mkII Processor wrapper for the tape-style varispeed oscillator.
 *
 */

#include "tapeosc_engine.h"
#include "processor.h"
#include <stdint.h>

class TapeOsc : public Processor
{
public:
  uint32_t getBufferSize() const override final { return 0; }

  void setParameter(uint8_t index, int32_t value) override final
  {
    engine_.applyParam(index, value);
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    return engine_.parameterString(index, value);
  }

  void init(float *) override final
  {
    engine_.setDefaults();
    engine_.reset();
    engine_.randomizePhase();
    base_note_ = 60.f;
    base_w0_ = 261.625565f * (1.f / getSampleRate());
    engine_.setPitch(base_w0_, base_note_);
  }

  void reset() override final
  {
    engine_.reset();
    engine_.randomizePhase();
  }

  void setPitch(float w0)
  {
    base_w0_ = w0;
    engine_.setPitch(base_w0_, base_note_);
  }

  void setNote(float note)
  {
    base_note_ = note;
    engine_.setPitch(base_w0_, base_note_);
  }

  void noteOn(uint8_t note, uint8_t velo) override final
  {
    (void)velo;
    base_note_ = static_cast<float>(note);
    engine_.setPitch(base_w0_, base_note_);
    engine_.randomizePhase();
    engine_.beginStart();
  }

  void noteOff(uint8_t note) override final
  {
    // The amp envelope after this oscillator owns the release tail.
    (void)note;
  }

  void allNoteOff() override final
  {
    engine_.reset();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
      out[sampleIndex] = engine_.render();
  }

private:
  TapeOscEngine engine_;
  float base_w0_ = 0.f;
  float base_note_ = 60.f;
};
