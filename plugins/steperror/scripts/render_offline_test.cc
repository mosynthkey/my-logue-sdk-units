#include "steperror.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static float windowRms(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  double sum = 0.0;
  uint32_t used = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const float sample = mono[index];
    sum += static_cast<double>(sample) * static_cast<double>(sample);
    ++used;
  }
  if (used == 0U)
    return 0.f;
  return static_cast<float>(std::sqrt(sum / static_cast<double>(used)));
}

static void fillTone(std::vector<float> &left, std::vector<float> &right, float hz, float amp)
{
  const float phase_inc = 6.28318530718f * hz / 48000.f;
  float phase = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < left.size(); ++sampleIndex)
  {
    const float sample = std::sin(phase) * amp;
    left[sampleIndex] = sample;
    right[sampleIndex] = sample * 0.85f;
    phase += phase_inc;
    if (phase > 6.28318530718f)
      phase -= 6.28318530718f;
  }
}

static void renderMono(StepError &fx, const std::vector<float> &left, const std::vector<float> &right,
                       std::vector<float> &mono_out)
{
  const uint32_t block_frames = 64U;
  std::vector<float> in_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  const uint32_t frames = static_cast<uint32_t>(left.size());
  mono_out.clear();
  mono_out.reserve(frames);
  while (frameOffset < frames)
  {
    const uint32_t this_block = (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
    {
      const uint32_t sourceIndex = frameOffset + sampleIndex;
      in_block[sampleIndex * 2U] = left[sourceIndex];
      in_block[sampleIndex * 2U + 1U] = right[sourceIndex];
    }
    fx.process(in_block.data(), out_block.data(), this_block);
    for (uint32_t sampleIndex = 0; sampleIndex < this_block; ++sampleIndex)
      mono_out.push_back(out_block[sampleIndex * 2U]);
    frameOffset += this_block;
  }
}

static void setup(StepError &fx, int32_t prob, int32_t err, int32_t mix, uint8_t kind)
{
  fx.setTempo(120.f);
  fx.setParameter(StepError::PROB, prob);
  fx.setParameter(StepError::ERR, err);
  fx.setParameter(StepError::MIX, mix);
  fx.setParameter(StepError::STEPS, StepError::PERIOD_1STEP);
  fx.setParameter(StepError::KIND, kind);
}

int main()
{
  const uint32_t frames = 48000U;
  std::vector<float> left(frames, 0.f);
  std::vector<float> right(frames, 0.f);
  fillTone(left, right, 440.f, 0.4f);
  const float dry_rms = windowRms(left, 12000U, 8000U);

  StepError dry_fx;
  std::vector<float> ram(dry_fx.getBufferSize(), 0.f);
  dry_fx.init(ram.data());
  setup(dry_fx, 1023, 1023, 1000, StepError::KIND_DROP);
  std::vector<float> untouched;
  renderMono(dry_fx, left, right, untouched);
  const float untouched_err = std::fabs(windowRms(untouched, 12000U, 8000U) - dry_rms);
  if (untouched_err > 0.02f)
  {
    std::fprintf(stderr, "FAIL: no-touch should stay dry (err=%.4f)\n", untouched_err);
    return 1;
  }

  StepError drop_fx;
  std::vector<float> drop_ram(drop_fx.getBufferSize(), 0.f);
  drop_fx.init(drop_ram.data());
  setup(drop_fx, 1023, 1023, 1000, StepError::KIND_DROP);
  drop_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> dropped;
  renderMono(drop_fx, left, right, dropped);
  const float drop_rms = windowRms(dropped, 12000U, 8000U);
  if (drop_rms > dry_rms * 0.25f)
  {
    std::fprintf(stderr, "FAIL: DROP should mute (rms=%.4f dry=%.4f)\n", drop_rms, dry_rms);
    return 1;
  }

  StepError crush_fx;
  std::vector<float> crush_ram(crush_fx.getBufferSize(), 0.f);
  crush_fx.init(crush_ram.data());
  setup(crush_fx, 1023, 900, 1000, StepError::KIND_CRUSH);
  crush_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> crushed;
  renderMono(crush_fx, left, right, crushed);
  const float crush_rms = windowRms(crushed, 12000U, 8000U);
  if (crush_rms < 0.02f || crush_rms > 1.5f)
  {
    std::fprintf(stderr, "FAIL: CRUSH level out of range (rms=%.4f)\n", crush_rms);
    return 1;
  }

  StepError skip_fx;
  std::vector<float> skip_ram(skip_fx.getBufferSize(), 0.f);
  skip_fx.init(skip_ram.data());
  setup(skip_fx, 1023, 700, 1000, StepError::KIND_SKIP);
  skip_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> skipped;
  renderMono(skip_fx, left, right, skipped);
  const float skip_rms = windowRms(skipped, 12000U, 8000U);
  if (skip_rms < 0.01f || skip_rms > 1.5f)
  {
    std::fprintf(stderr, "FAIL: SKIP level out of range (rms=%.4f)\n", skip_rms);
    return 1;
  }

  StepError zero_fx;
  std::vector<float> zero_ram(zero_fx.getBufferSize(), 0.f);
  zero_fx.init(zero_ram.data());
  setup(zero_fx, 0, 1023, 1000, StepError::KIND_ALL);
  zero_fx.touchEvent(0, k_unit_touch_phase_began, 512U, 512U);
  std::vector<float> zero_out;
  renderMono(zero_fx, left, right, zero_out);
  const float zero_err = std::fabs(windowRms(zero_out, 12000U, 8000U) - dry_rms);
  if (zero_err > 0.03f)
  {
    std::fprintf(stderr, "FAIL: PROB=0 should stay dry (err=%.4f)\n", zero_err);
    return 1;
  }

  std::printf("OK dry=%.4f drop=%.4f crush=%.4f skip=%.4f\n", dry_rms, drop_rms, crush_rms, skip_rms);
  return 0;
}
