/*
 * Host-side probe for Passort bottom-right step roll loop length.
 *
 * Build:
 *   g++ -O2 -std=c++11 \
 *     -I plugins/passort/dsp -I plugins/common \
 *     -I third_party/logue-sdk/platform/nts-3_kaoss/common \
 *     -I third_party/logue-sdk/platform/nts-3_kaoss \
 *     plugins/passort/scripts/roll_probe.cc -o /tmp/passort_roll_probe
 */
#include "passort.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static void fillTone(std::vector<float> &left, std::vector<float> &right, float hz, float amp)
{
  const float phase_inc = 6.28318530718f * hz / 48000.f;
  float phase = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < left.size(); ++sampleIndex)
  {
    const float sample = sinf(phase) * amp;
    left[sampleIndex] = sample;
    right[sampleIndex] = sample * 0.85f;
    phase += phase_inc;
    if (phase > 6.28318530718f)
      phase -= 6.28318530718f;
  }
}

static void renderBlocks(Passort &fx, const float *left, const float *right, uint32_t frames)
{
  const uint32_t block_frames = 64U;
  std::vector<float> dry_block(block_frames * 2U, 0.f);
  std::vector<float> raw_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  while (frameOffset < frames)
  {
    const uint32_t this_block =
        (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      dry_block[sampleIndex * 2U] = left[sourceIndex];
      dry_block[sampleIndex * 2U + 1U] = right[sourceIndex];
      raw_block[sampleIndex * 2U] = left[sourceIndex];
      raw_block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(dry_block.data(), raw_block.data(), out_block.data(), this_block);
    frameOffset += this_block;
  }
}

static bool probeCorner(const char *label, uint32_t touch_x, uint32_t touch_y,
                        uint32_t min_loop_samples, uint32_t max_loop_samples = 0U)
{
  Passort fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(Passort::MIX, 1000);
  fx.setParameter(Passort::GLUE, 256);

  const uint32_t preroll = 120000U;
  const uint32_t held = 48000U;
  std::vector<float> left(preroll + held, 0.f);
  std::vector<float> right(preroll + held, 0.f);
  fillTone(left, right, 220.f, 0.4f);

  renderBlocks(fx, left.data(), right.data(), preroll);
  fx.touchEvent(0, k_unit_touch_phase_began, touch_x, touch_y);
  renderBlocks(fx, left.data() + preroll, right.data() + preroll, held);

  const uint32_t loop_length = fx.rollLoopLength();
  const bool mode_ok = fx.currentMode() == Passort::MODE_ROLL;
  const bool length_ok =
      loop_length >= min_loop_samples &&
      (max_loop_samples == 0U || loop_length <= max_loop_samples);
  std::printf("%s mode=%u loop=%u (min %u max %u) %s\n", label, fx.currentMode(),
              loop_length, min_loop_samples, max_loop_samples,
              (mode_ok && length_ok) ? "OK" : "FAIL");
  return mode_ok && length_ok;
}

static bool probeGesture(const char *label, uint32_t start_x, uint32_t start_y,
                         uint32_t move_x, uint32_t move_y, uint32_t min_loop_samples)
{
  Passort fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(Passort::MIX, 1000);
  fx.setParameter(Passort::GLUE, 256);

  const uint32_t preroll = 120000U;
  const uint32_t settle = 48000U;
  std::vector<float> left(preroll + settle * 2U, 0.f);
  std::vector<float> right(preroll + settle * 2U, 0.f);
  fillTone(left, right, 220.f, 0.4f);

  renderBlocks(fx, left.data(), right.data(), preroll);
  fx.touchEvent(0, k_unit_touch_phase_began, start_x, start_y);
  renderBlocks(fx, left.data() + preroll, right.data() + preroll, settle);
  fx.touchEvent(0, k_unit_touch_phase_moved, move_x, move_y);
  renderBlocks(fx, left.data() + preroll + settle, right.data() + preroll + settle, settle);

  const uint32_t loop_length = fx.rollLoopLength();
  const bool mode_ok = fx.currentMode() == Passort::MODE_ROLL;
  const bool length_ok = loop_length >= min_loop_samples;
  std::printf("%s mode=%u loop=%u (min %u) %s\n", label, fx.currentMode(), loop_length,
              min_loop_samples, (mode_ok && length_ok) ? "OK" : "FAIL");
  return mode_ok && length_ok;
}

int main()
{
  // At 120 BPM / 48 kHz: beat=24000. Fastest grid = beat/32 = 750 samples.
  // Corner start should be well above the tonal floor (256).
  const uint32_t floor_samples = Passort::kMinRollLoopSamples;
  const uint32_t sixteenth = 24000U / 16U; // 1500
  const uint32_t half_beat = 24000U / 2U;  // 12000
  const uint32_t beat32 = 24000U / 32U;    // 750

  bool ok = true;
  // Bottom-right corner: long buffer + slow grid → ~half-beat slice.
  ok = probeCorner("br_corner", 980U, 80U, half_beat / 2U) && ok;
  // Top-right of roll quadrant: long buffer + fast grid → >= beat/32.
  ok = probeCorner("br_fast", 980U, 400U, sixteenth / 2U) && ok;
  // Toward center-left of roll quadrant: shorter buffer still >= floor.
  ok = probeCorner("br_short", 560U, 80U, floor_samples) && ok;
  // Lock ROLL at BR, then drag to short+fast extreme — must stay audible.
  ok = probeGesture("br_to_short_fast", 980U, 80U, 40U, 1000U, beat32) && ok;

  if (!ok)
  {
    std::printf("roll probe FAILED\n");
    return 1;
  }
  std::printf("roll probe passed\n");
  return 0;
}
