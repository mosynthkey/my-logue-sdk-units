#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static void render_blocks(Ride909 &ride, uint32_t blocks, std::vector<float> &mono_out)
{
  constexpr uint32_t kBlockSize = 128U;
  std::vector<float> buffer(kBlockSize * 2U, 0.f);
  mono_out.assign(blocks * kBlockSize, 0.f);

  for (uint32_t blockIndex = 0; blockIndex < blocks; ++blockIndex)
  {
    // process() is dry+wet; clear so in-place render does not accumulate.
    std::fill(buffer.begin(), buffer.end(), 0.f);
    ride.process(buffer.data(), buffer.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize; ++sampleIndex)
      mono_out[blockIndex * kBlockSize + sampleIndex] = buffer[sampleIndex * 2U];
  }
}

int main()
{
  Ride909 no_pump;
  Ride909 full_pump;

  std::vector<float> dry;
  std::vector<float> wet;

  no_pump.init(nullptr);
  no_pump.setParameter(Ride909::MIX, 1000);
  no_pump.setParameter(Ride909::PUMP, 0);
  no_pump.setParameter(Ride909::PITCH, 512);
  no_pump.setTempo(128.f);
  no_pump.touchEvent(0, k_unit_touch_phase_began, 512, 0);
  render_blocks(no_pump, 800U, dry);

  full_pump.init(nullptr);
  full_pump.setParameter(Ride909::MIX, 1000);
  full_pump.setParameter(Ride909::PUMP, 1023);
  full_pump.setParameter(Ride909::PITCH, 512);
  full_pump.setTempo(128.f);
  full_pump.touchEvent(0, k_unit_touch_phase_began, 512, 0);
  render_blocks(full_pump, 800U, wet);

  float max_delta = 0.f;
  for (size_t sampleIndex = 0; sampleIndex < dry.size(); ++sampleIndex)
    max_delta = std::fmax(max_delta, std::fabs(dry[sampleIndex] - wet[sampleIndex]));

  double dry_sq = 0.0;
  double wet_sq = 0.0;
  for (size_t sampleIndex = 0; sampleIndex < dry.size(); ++sampleIndex)
  {
    dry_sq += dry[sampleIndex] * dry[sampleIndex];
    wet_sq += wet[sampleIndex] * wet[sampleIndex];
  }
  const float dry_rms = static_cast<float>(std::sqrt(dry_sq / static_cast<double>(dry.size())));
  const float wet_rms = static_cast<float>(std::sqrt(wet_sq / static_cast<double>(wet.size())));

  std::printf("dry_rms=%.6f wet_rms=%.6f rms_ratio=%.3f max_delta=%.6f\n", dry_rms, wet_rms,
              wet_rms / dry_rms, max_delta);
  return (max_delta > 0.06f && wet_rms < dry_rms * 0.88f) ? 0 : 1;
}
