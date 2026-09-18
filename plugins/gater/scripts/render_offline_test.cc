#include "gater.h"
#include "runtime.h"

#include <cmath>
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

static void renderWithSplitInput(Gater &fx, const float *dry_left, const float *dry_right,
                                 const float *raw_left, const float *raw_right, uint32_t frames,
                                 std::vector<float> &mono_out)
{
  const uint32_t block_frames = 64U;
  std::vector<float> dry_block(block_frames * 2U, 0.f);
  std::vector<float> raw_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  while (frameOffset < frames)
  {
    const uint32_t this_block = (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      dry_block[sampleIndex * 2U] = dry_left[sourceIndex];
      dry_block[sampleIndex * 2U + 1U] = dry_right[sourceIndex];
      raw_block[sampleIndex * 2U] = raw_left[sourceIndex];
      raw_block[sampleIndex * 2U + 1U] = raw_right[sourceIndex];
    }
    fx.process(dry_block.data(), raw_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }
}

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

static void setup(Gater &fx)
{
  fx.setTempo(120.f);
  fx.setParameter(Gater::MIX, 1000);
  fx.setParameter(Gater::DECAY, 256);
  fx.setParameter(Gater::SYNC, Gater::SYNC_EVEN);
  fx.setParameter(Gater::HOLD, Gater::HOLD_GATE);
}

int main()
{
  bool ok = true;
  Gater fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  setup(fx);

  const uint32_t preroll = 100000U;
  const uint32_t held = 24000U;
  std::vector<float> left(preroll + held, 0.f);
  std::vector<float> right(preroll + held, 0.f);
  fillTone(left, right, 220.f, 0.35f);

  std::vector<float> mono;
  mono.reserve(preroll + held);

  // Pre-roll without touch so capture fills.
  renderWithSplitInput(fx, left.data(), right.data(), left.data(), right.data(), preroll, mono);
  fx.touchEvent(0, k_unit_touch_phase_began, 512U, 700U);
  renderWithSplitInput(fx, left.data() + preroll, right.data() + preroll,
                       left.data() + preroll, right.data() + preroll, held, mono);

  const float wet_rms = windowRms(mono, preroll + 2000U, 8000U);
  const float dry_rms = windowRms(mono, 1000U, 8000U);
  const bool engaged = fx.isActive() && fx.wetAmount() > 0.5f;
  const bool louder_or_modulated = wet_rms > 0.01f;
  std::printf("gater_preroll_rms=%.4f wet_rms=%.4f active=%d wet=%.3f %s\n",
              dry_rms, wet_rms, fx.isActive() ? 1 : 0, fx.wetAmount(),
              (engaged && louder_or_modulated) ? "OK" : "FAIL");
  if (!(engaged && louder_or_modulated))
    ok = false;

  fx.touchEvent(0, k_unit_touch_phase_ended, 512U, 700U);
  std::vector<float> release_mono;
  std::vector<float> silence_l(8000U, 0.f);
  std::vector<float> silence_r(8000U, 0.f);
  // Keep feeding tone while releasing to verify bypass return.
  std::vector<float> tone_l(8000U, 0.f);
  std::vector<float> tone_r(8000U, 0.f);
  fillTone(tone_l, tone_r, 220.f, 0.35f);
  renderWithSplitInput(fx, tone_l.data(), tone_r.data(), tone_l.data(), tone_r.data(), 8000U, release_mono);
  const bool released = !fx.isPadHeld() && fx.wetAmount() < 0.05f;
  std::printf("gater_release wet=%.3f held=%d %s\n", fx.wetAmount(), fx.isPadHeld() ? 1 : 0,
              released ? "OK" : "FAIL");
  if (!released)
    ok = false;

  std::printf("gater_offline %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
