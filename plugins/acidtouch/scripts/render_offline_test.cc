/*
 * AcidTouch host probe. Copyright (C) 2026 acidtouch contributors
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define ACIDTOUCH_OFFLINE_TEST

#include "acidtouch.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static float windowRms(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  double sum_squares = 0.0;
  uint32_t used = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const double sample = static_cast<double>(mono[index]);
    sum_squares += sample * sample;
    ++used;
  }
  if (used == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(sum_squares / static_cast<double>(used)));
}

static void renderBlock(AcidTouch &synth, uint32_t frames, std::vector<float> &mono_accum)
{
  std::vector<float> block(frames * 2U, 0.f);
  synth.process(block.data(), block.data(), frames);
  for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    mono_accum.push_back(block[sampleIndex * 2U]);
}

static uint64_t fingerprint(const AcidTouch &synth)
{
  uint64_t value = 0U;
  for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
  {
    value *= 41U;
    value += static_cast<uint64_t>(synth.debugNote(stepIndex) + 2);
    value *= 3U;
    value += synth.debugSlide(stepIndex) ? 1U : 0U;
    value *= 3U;
    value += synth.debugAccent(stepIndex) ? 1U : 0U;
  }
  return value;
}

static int fail(int code, const char *message)
{
  std::printf("fail %d: %s\n", code, message);
  return code;
}

int main()
{
  AcidTouch synth;
  synth.init(nullptr);
  synth.setTempo(120.f);
  synth.setParameter(AcidTouch::CUT, 512);
  synth.setParameter(AcidTouch::RES, 700);
  synth.setParameter(AcidTouch::DEN, 640);
  synth.setParameter(AcidTouch::TIME, 512);
  synth.setParameter(AcidTouch::ACID, 700);
  synth.setParameter(AcidTouch::OCT, 1);
  synth.setParameter(AcidTouch::ROOT, 36);
  synth.setParameter(AcidTouch::MODE, 4);

  const uint32_t step_samples = synth.debugStepSamples();
  std::printf("step_samples=%u base_hz=%.1f res_k=%.2f\n", step_samples, synth.debugBaseHz(),
              synth.debugResK());
  if (step_samples < 5800U || step_samples > 6200U)
    return fail(1, "16th length at 120 BPM");

  synth.setTempo(60.f);
  const uint32_t slow_steps = synth.debugStepSamples();
  synth.setTempo(120.f);
  if (slow_steps < 11600U || slow_steps > 12400U)
    return fail(2, "tempo doubles the step");

  synth.setParameter(AcidTouch::CUT, 0);
  if (synth.debugBaseHz() < 70.f || synth.debugBaseHz() > 100.f)
    return fail(3, "cutoff floor");
  synth.setParameter(AcidTouch::CUT, 1023);
  if (synth.debugBaseHz() < 7000.f || synth.debugBaseHz() > 9000.f)
    return fail(4, "cutoff ceiling");
  synth.setParameter(AcidTouch::CUT, 512);

  synth.setParameter(AcidTouch::RES, 0);
  if (synth.debugResK() > 0.05f)
    return fail(5, "resonance floor");
  synth.setParameter(AcidTouch::RES, 1023);
  if (synth.debugResK() < 3.2f || synth.debugResK() > 3.5f)
    return fail(6, "resonance cap");
  synth.setParameter(AcidTouch::RES, 700);

  synth.setParameter(AcidTouch::DEN, 0);
  synth.setParameter(AcidTouch::ACID, 0);
  synth.debugGenerate(0x12345678U);
  const uint64_t sparse_a = fingerprint(synth);
  synth.debugGenerate(0x12345678U);
  if (fingerprint(synth) != sparse_a)
    return fail(7, "seed is not stable");
  if (synth.debugNoteCount() < 1U || synth.debugNoteCount() > 3U)
    return fail(8, "low density still has a note and stays sparse");
  uint32_t slides = 0U;
  for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
  {
    if (synth.debugSlide(stepIndex))
      ++slides;
  }
  if (slides != 0U)
    return fail(9, "acid 0 should not slide");

  synth.setParameter(AcidTouch::ROOT, 41);
  synth.debugGenerate(0x51U);
  static const int kMinorPc[] = {0, 2, 3, 5, 7, 8, 10};
  uint32_t rooted = 0U;
  for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
  {
    const int note = synth.debugNote(stepIndex);
    if (note < 0)
      continue;
    ++rooted;
    if (note < 41 || note > 41 + 34)
      return fail(26, "note left the root range");
    const int pitch_class = (note - 41) % 12;
    bool in_scale = false;
    for (uint32_t degreeIndex = 0; degreeIndex < 7U; ++degreeIndex)
    {
      if (pitch_class == kMinorPc[degreeIndex])
        in_scale = true;
    }
    if (!in_scale)
      return fail(27, "note is outside natural minor of root");
  }
  if (rooted == 0U)
    return fail(28, "root phrase was empty");
  const int before = synth.debugNote(0);
  synth.setParameter(AcidTouch::ROOT, 46);
  if (before >= 0 && synth.debugNote(0) != before + 5)
    return fail(29, "root knob did not transpose the phrase");
  synth.setParameter(AcidTouch::ROOT, 36);

  synth.setParameter(AcidTouch::DEN, 1023);
  synth.setParameter(AcidTouch::ACID, 1023);
  synth.debugGenerate(0x89ABCDEFU);
  if (synth.debugNoteCount() != AcidTouch::kSteps)
    return fail(10, "full density fills every step");
  slides = 0U;
  uint32_t accents = 0U;
  for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
  {
    if (synth.debugSlide(stepIndex))
      ++slides;
    if (synth.debugAccent(stepIndex))
      ++accents;
  }
  if (slides < 8U)
    return fail(11, "high acid should slide often");
  if (accents < 8U)
    return fail(12, "high acid should accent often");

  synth.setParameter(AcidTouch::DEN, 640);
  synth.setParameter(AcidTouch::ACID, 620);
  uint32_t unique = 0U;
  uint64_t seen[24];
  uint32_t min_notes = 16U;
  for (uint32_t rollIndex = 0; rollIndex < 24U; ++rollIndex)
  {
    synth.debugRenew();
    if (synth.debugNoteCount() < min_notes)
      min_notes = synth.debugNoteCount();
    if (synth.debugNoteCount() == 0U)
      return fail(13, "renew produced silence");
    const uint64_t id = fingerprint(synth);
    bool fresh = true;
    for (uint32_t seenIndex = 0; seenIndex < unique; ++seenIndex)
    {
      if (seen[seenIndex] == id)
        fresh = false;
    }
    if (fresh)
      seen[unique++] = id;
  }
  std::printf("unique=%u min_notes=%u\n", unique, min_notes);
  if (unique < 20U)
    return fail(14, "renew does not vary the phrase");

  const uint64_t before_mut = fingerprint(synth);
  uint32_t changed_sum = 0U;
  for (uint32_t mutIndex = 0; mutIndex < 16U; ++mutIndex)
  {
    int8_t notes_before[AcidTouch::kSteps];
    for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
      notes_before[stepIndex] = synth.debugNote(stepIndex);
    synth.debugMutate();
    uint32_t changed = 0U;
    for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
    {
      if (synth.debugNote(stepIndex) != notes_before[stepIndex])
        ++changed;
    }
    changed_sum += changed;
    if (synth.debugNoteCount() == 0U)
      return fail(15, "mutate emptied the phrase");
  }
  const float changed_mean = static_cast<float>(changed_sum) / 16.f;
  std::printf("mutate_mean=%.2f\n", changed_mean);
  if (changed_mean < 1.5f || changed_mean > 8.f)
    return fail(16, "mutate rate is not near 25%");
  if (fingerprint(synth) == before_mut)
    return fail(17, "mutate left the phrase untouched");

  synth.setParameter(AcidTouch::MODE, 0);
  if (synth.debugPlay() != AcidTouch::PLAY_GATE || synth.debugRunning())
    return fail(18, "gate is silent until touch");

  std::vector<float> silent;
  renderBlock(synth, 2048U, silent);
  if (windowRms(silent, 0U, 2048U) > 0.002f)
    return fail(19, "gate mode leaked audio");

  synth.touchEvent(0, k_unit_touch_phase_began, 100, 100);
  std::printf("running=%d notes=%u step0=%d\n", synth.debugRunning() ? 1 : 0, synth.debugNoteCount(),
              synth.debugNote(0));
  std::vector<float> held;
  renderBlock(synth, 96000U, held);
  const float held_rms = windowRms(held, 0U, static_cast<uint32_t>(held.size()));
  uint32_t first_note = AcidTouch::kSteps;
  for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
  {
    if (synth.debugNote(stepIndex) >= 0)
    {
      first_note = stepIndex;
      break;
    }
  }
  if (first_note >= AcidTouch::kSteps)
    return fail(20, "touch produced an empty phrase");
  const uint32_t note_at = first_note * synth.debugStepSamples();
  const float attack_rms = windowRms(held, note_at, 96U);
  const float body_rms = windowRms(held, note_at + 700U, 1800U);
  std::printf("held_rms=%.4f attack=%.4f body=%.4f\n", held_rms, attack_rms, body_rms);
  if (held_rms < 0.015f || body_rms < 0.02f)
    return fail(21, "note body is a click or silence");

  synth.touchEvent(0, k_unit_touch_phase_ended, 100, 100);
  std::vector<float> released;
  renderBlock(synth, 8000U, released);
  if (windowRms(released, 4000U, 3000U) > 0.003f)
    return fail(22, "gate release did not stop");

  synth.setParameter(AcidTouch::MODE, 2);
  synth.touchEvent(0, k_unit_touch_phase_began, 200, 200);
  synth.touchEvent(0, k_unit_touch_phase_ended, 200, 200);
  if (!synth.debugRunning())
    return fail(23, "latch stopped after release");
  std::vector<float> latched;
  renderBlock(synth, 48000U, latched);
  if (windowRms(latched, 0U, static_cast<uint32_t>(latched.size())) < 0.015f)
    return fail(23, "latch went silent after release");

  bool found_slide = false;
  uint32_t slide_source = 0U;
  for (uint32_t attempt = 0; attempt < 12U && !found_slide; ++attempt)
  {
    synth.touchEvent(0, k_unit_touch_phase_ended, 10, 10);
    synth.touchEvent(0, k_unit_touch_phase_began, 10, 10);
    for (uint32_t stepIndex = 0; stepIndex < AcidTouch::kSteps; ++stepIndex)
    {
      const uint32_t next_index = (stepIndex + 1U) % AcidTouch::kSteps;
      if (synth.debugSlide(stepIndex) && synth.debugNote(stepIndex) >= 0 &&
          synth.debugNote(next_index) >= 0 &&
          synth.debugNote(stepIndex) != synth.debugNote(next_index))
      {
        found_slide = true;
        slide_source = stepIndex;
        break;
      }
    }
  }
  if (!found_slide)
    return fail(24, "no sliding interval");

  const float source_pitch = static_cast<float>(synth.debugNote(slide_source));
  const float dest_pitch = static_cast<float>(synth.debugNote((slide_source + 1U) % AcidTouch::kSteps));
  const uint32_t slide_at = (slide_source + 1U) * synth.debugStepSamples();
  std::vector<float> glide;
  uint32_t rendered = 0U;
  float max_progress = 0.f;
  bool saw_slide = false;
  while (rendered < slide_at + 5000U)
  {
    renderBlock(synth, 64U, glide);
    rendered += 64U;
    if (rendered >= slide_at && rendered < slide_at + 128U)
      saw_slide = synth.debugSlideActive();
    if (rendered >= slide_at && rendered < slide_at + 4000U)
    {
      float progress = (synth.debugPitch() - source_pitch) / (dest_pitch - source_pitch);
      if (progress < 0.f)
        progress = 0.f;
      if (progress > max_progress)
        max_progress = progress;
    }
  }
  std::printf("slide_progress=%.2f active=%d\n", max_progress, saw_slide ? 1 : 0);
  if (!saw_slide || max_progress < 0.35f)
    return fail(25, "slide glide did not move");

  std::printf("ok\n");
  return 0;
}
