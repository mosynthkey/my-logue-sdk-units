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
    held_count_ = 0;
    base_note_ = 60.f;
    base_w0_ = 261.625565f * (1.f / getSampleRate());
    engine_.setPitch(base_w0_, base_note_);
    active_note_ = 0xFF;
  }

  void reset() override final
  {
    engine_.reset();
    engine_.randomizePhase();
    held_count_ = 0;
    active_note_ = 0xFF;
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
    bool already_held = false;
    for (uint8_t heldIndex = 0; heldIndex < held_count_; ++heldIndex)
    {
      if (held_notes_[heldIndex] == note)
        already_held = true;
    }
    if (!already_held && held_count_ < kMaxHeld)
    {
      held_notes_[held_count_] = note;
      ++held_count_;
    }

    active_note_ = note;
    base_note_ = static_cast<float>(note);
    engine_.setPitch(base_w0_, base_note_);
    engine_.randomizePhase();
    engine_.beginStart();
  }

  void noteOff(uint8_t note) override final
  {
    // 0 and 0xFF are gate-style offs. An off that is not in the held set used
    // to be ignored, so releasing the key never started the motor stop.
    if (note == 0U || note == 0xFFU)
    {
      held_count_ = 0;
    }
    else
    {
      uint8_t remaining = 0;
      bool found = false;
      for (uint8_t heldIndex = 0; heldIndex < held_count_; ++heldIndex)
      {
        if (held_notes_[heldIndex] == note)
          found = true;
        else
          held_notes_[remaining++] = held_notes_[heldIndex];
      }
      if (!found)
        remaining = 0;
      held_count_ = remaining;
    }

    if (held_count_ == 0)
    {
      active_note_ = 0xFF;
      engine_.beginStop();
    }
  }

  void allNoteOff() override final
  {
    held_count_ = 0;
    active_note_ = 0xFF;
    engine_.beginStop();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    (void)in;

    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
      out[sampleIndex] = engine_.render();
  }

private:
  static const uint8_t kMaxHeld = 8;

  TapeOscEngine<> engine_;
  float base_w0_ = 0.f;
  float base_note_ = 60.f;
  uint8_t held_notes_[kMaxHeld] = {};
  uint8_t held_count_ = 0;
  uint8_t active_note_ = 0xFF;
};
