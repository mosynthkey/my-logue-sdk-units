#include "stepgrain.h"
#include "runtime.h"
#include <cstdio>
#include <string>
#include <vector>

int main()
{
  constexpr uint32_t kFrames = 128U;
  StepGrain fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(StepGrain::MIX, 100);
  fx.setParameter(StepGrain::DENS, 800);
  fx.setParameter(StepGrain::OCT, 700);
  fx.setParameter(StepGrain::ENV, StepGrain::SEAM_4STEP);
  fx.setParameter(StepGrain::STEPS, StepGrain::PERIOD_1STEP);
  fx.setParameter(StepGrain::SPRD, 100);
  fx.setParameter(StepGrain::MODE, StepGrain::MODE_FREQ);
  fx.setParameter(StepGrain::REVS, 50);

  // String Desc path (dummy-genericfx style switch tables)
  if (std::string(fx.getParameterStrValue(StepGrain::ENV, StepGrain::SEAM_4STEP)) != "4 St")
    return 2;
  if (std::string(fx.getParameterStrValue(StepGrain::ENV, StepGrain::SEAM_OFF)) != "Off")
    return 3;
  if (fx.getParameterStrValue(StepGrain::ENV, 5) != nullptr)
    return 4;
  if (std::string(fx.getParameterStrValue(StepGrain::STEPS, StepGrain::PERIOD_1STEP)) != "1 St")
    return 5;
  if (std::string(fx.getParameterStrValue(StepGrain::STEPS, StepGrain::PERIOD_HALF)) != "Half")
    return 6;
  if (std::string(fx.getParameterStrValue(StepGrain::MODE, StepGrain::MODE_VOLUME)) != "Volume")
    return 7;
  if (std::string(fx.getParameterStrValue(StepGrain::MODE, StepGrain::MODE_FREQ)) != "Freq")
    return 8;
  if (fx.getParameterStrValue(StepGrain::MIX, 100) != nullptr)
    return 9;

  std::vector<float> input(kFrames * 2U, 0.f);
  std::vector<float> output(kFrames * 2U, 0.f);

  for (int blockIndex = 0; blockIndex < 400; ++blockIndex)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kFrames; ++sampleIndex)
    {
      const float phase = static_cast<float>((blockIndex * static_cast<int>(kFrames) + static_cast<int>(sampleIndex)) % 96) / 96.f;
      const float sample = (phase < 0.5f ? phase * 4.f - 1.f : 3.f - phase * 4.f) * 0.25f;
      input[sampleIndex * 2U] = sample;
      input[sampleIndex * 2U + 1U] = sample * 0.85f;
    }
    fx.process(input.data(), input.data(), output.data(), kFrames);
  }

  fx.touchEvent(0, k_unit_touch_phase_began, 800, 512);

  float peak = 0.f;
  float late_abs = 0.f;
  uint32_t late_count = 0U;
  uint32_t rendered = 0U;
  const uint32_t late_start = static_cast<uint32_t>(fx.getSampleRate() * 0.12f);
  const uint32_t late_end = static_cast<uint32_t>(fx.getSampleRate() * 0.22f);

  for (int blockIndex = 0; blockIndex < 300; ++blockIndex)
  {
    fx.process(input.data(), input.data(), output.data(), kFrames);
    for (uint32_t sampleIndex = 0; sampleIndex < kFrames; ++sampleIndex)
    {
      const float left = output[sampleIndex * 2U];
      const float right = output[sampleIndex * 2U + 1U];
      const float abs_left = left < 0.f ? -left : left;
      const float abs_right = right < 0.f ? -right : right;
      const float abs_sample = abs_left > abs_right ? abs_left : abs_right;
      if (abs_sample > peak)
        peak = abs_sample;
      if (rendered >= late_start && rendered < late_end)
      {
        late_abs += abs_sample;
        ++late_count;
      }
      ++rendered;
    }
  }

  fx.touchEvent(0, k_unit_touch_phase_ended, 800, 512);
  for (int blockIndex = 0; blockIndex < 100; ++blockIndex)
    fx.process(input.data(), input.data(), output.data(), kFrames);

  const float late_mean = late_count > 0U ? late_abs / static_cast<float>(late_count) : 0.f;
  std::printf("stepgrain_offline_peak=%.6f late_mean=%.6f\n", peak, late_mean);
  return (peak > 0.001f && late_mean > 0.001f) ? 0 : 1;
}
