#include "stepgrain2.h"
#include "runtime.h"
#include <cstdio>
#include <string>
#include <vector>

int main()
{
  constexpr uint32_t kFrames = 128U;
  StepGrain2 fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(StepGrain2::MIX, 100);
  fx.setParameter(StepGrain2::FADE, 50);
  fx.setParameter(StepGrain2::PROBABILITY, 0);
  fx.setParameter(StepGrain2::LENGTH, StepGrain2::LENGTH_4);
  fx.setParameter(StepGrain2::SHIMMER, 100);
  fx.setParameter(StepGrain2::SPREAD, 0);
  fx.setParameter(StepGrain2::REVERSE, 0);

  if (std::string(fx.getParameterStrValue(StepGrain2::LENGTH, StepGrain2::LENGTH_1)) != "1 Step")
    return 2;
  if (std::string(fx.getParameterStrValue(StepGrain2::LENGTH, StepGrain2::LENGTH_16)) != "16 Step")
    return 3;
  if (fx.getParameterStrValue(StepGrain2::LENGTH, 5) != nullptr)
    return 4;
  if (fx.getParameterStrValue(StepGrain2::SHIMMER, 100) != nullptr)
    return 5;

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

  fx.touchEvent(0, k_unit_touch_phase_began, 512, 0);

  float silent_peak = 0.f;
  for (int blockIndex = 0; blockIndex < 200; ++blockIndex)
  {
    fx.process(input.data(), input.data(), output.data(), kFrames);
    for (uint32_t sampleIndex = 0; sampleIndex < kFrames; ++sampleIndex)
    {
      const float left = output[sampleIndex * 2U];
      const float right = output[sampleIndex * 2U + 1U];
      const float abs_left = left < 0.f ? -left : left;
      const float abs_right = right < 0.f ? -right : right;
      const float abs_sample = abs_left > abs_right ? abs_left : abs_right;
      if (abs_sample > silent_peak)
        silent_peak = abs_sample;
    }
  }

  fx.setParameter(StepGrain2::PROBABILITY, 100);
  float peak = 0.f;
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
    }
  }

  std::printf("stepgrain2_silent_peak=%.6f peak=%.6f\n", silent_peak, peak);
  return (silent_peak < 0.0001f && peak > 0.01f) ? 0 : 1;
}
