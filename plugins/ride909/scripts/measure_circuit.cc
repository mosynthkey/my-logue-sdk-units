#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static uint8_t unpack_pcm6(uint32_t sample_index)
{
  const uint32_t bit_index = sample_index * 6U;
  const uint32_t byte_index = bit_index >> 3;
  const uint32_t shift = bit_index & 7U;
  const uint32_t pair = static_cast<uint32_t>(kRide909PcmPacked[byte_index]) |
                        (static_cast<uint32_t>(kRide909PcmPacked[byte_index + 1U]) << 8);
  return static_cast<uint8_t>((pair >> shift) & 0x3FU);
}

static void render_one_hit(Ride909 &ride, int32_t pitch, std::vector<float> &mono)
{
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 0);
  ride.setParameter(Ride909::PITCH, pitch);
  ride.setParameter(Ride909::TONE, 512);
  ride.setParameter(Ride909::DEC, 1023);
  ride.setTempo(21.f);
  ride.touchEvent(0, k_unit_touch_phase_began, 512, 0);
  // Relative 4-step cycle: 1=pump, 2=rest, 3=ride.
  ride.tempo4ppqnTick(1U);
  ride.tempo4ppqnTick(2U);
  ride.tempo4ppqnTick(3U);

  constexpr uint32_t kBlockSize = 128U;
  constexpr uint32_t kBlockCount = 500U;
  std::vector<float> block(kBlockSize * 2U, 0.f);
  mono.assign(kBlockSize * kBlockCount, 0.f);

  for (uint32_t blockIndex = 0; blockIndex < kBlockCount; ++blockIndex)
  {
    std::fill(block.begin(), block.end(), 0.f);

    ride.process(block.data(), block.data(), kBlockSize);
    for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize; ++sampleIndex)
      mono[blockIndex * kBlockSize + sampleIndex] = block[sampleIndex * 2U];
  }
}

static uint32_t audible_length(const std::vector<float> &mono, float peak)
{
  const float threshold = peak * 0.02f;
  uint32_t last = 0U;
  for (uint32_t sampleIndex = 0; sampleIndex < mono.size(); ++sampleIndex)
  {
    if (std::fabs(mono[sampleIndex]) >= threshold)
      last = sampleIndex;
  }
  return last;
}

int main()
{
  const uint8_t expected_head[] = {32, 34, 37, 19, 45, 55, 9, 48};
  for (uint32_t sampleIndex = 0; sampleIndex < 8U; ++sampleIndex)
  {
    if (unpack_pcm6(sampleIndex) != expected_head[sampleIndex])
    {
      std::printf("pcm unpack mismatch at %u\n", sampleIndex);
      return 1;
    }
  }

  if (kRide909PcmLength != 32768U || kRide909RomClockHz != 35000.f)
  {
    std::printf("unexpected ROM geometry\n");
    return 1;
  }

  Ride909 center;
  Ride909 up;
  std::vector<float> center_mono;
  std::vector<float> up_mono;
  render_one_hit(center, 512, center_mono);
  render_one_hit(up, 1023, up_mono);

  float center_peak = 0.f;
  float up_peak = 0.f;
  for (float sample : center_mono)
    center_peak = std::fmax(center_peak, std::fabs(sample));
  for (float sample : up_mono)
    up_peak = std::fmax(up_peak, std::fabs(sample));

  const uint32_t center_len = audible_length(center_mono, center_peak);
  const uint32_t up_len = audible_length(up_mono, up_peak);
  const float length_ratio = static_cast<float>(up_len) / static_cast<float>(center_len);

  std::printf("pcm_ok=1 center_peak=%.4f up_peak=%.4f center_len=%u up_len=%u ratio=%.3f\n",
              center_peak, up_peak, center_len, up_len, length_ratio);

  if (center_peak < 0.02f || up_peak < 0.02f)
    return 1;
  // Hardware max Tune is ≈ +9.54 st (≈1.74x ROM clock), so the hit is ~0.58x as long.
  if (length_ratio > 0.72f || length_ratio < 0.40f)
    return 1;
  return 0;
}
