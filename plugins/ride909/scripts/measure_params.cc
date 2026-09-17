#include "macros.h"
#include "ride909.h"
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static void render_one_hit(Ride909 &ride, int32_t pitch, int32_t tone, int32_t decay,
                           std::vector<float> &mono)
{
  ride.init(nullptr);
  ride.setParameter(Ride909::MIX, 1000);
  ride.setParameter(Ride909::PUMP, 0);
  ride.setParameter(Ride909::PITCH, pitch);
  ride.setParameter(Ride909::TONE, tone);
  ride.setParameter(Ride909::DEC, decay);
  ride.setTempo(21.f);
  ride.touchEvent(0, k_unit_touch_phase_began, 512, 0);
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

static float peak_of(const std::vector<float> &mono)
{
  float peak = 0.f;
  for (float sample : mono)
    peak = std::fmax(peak, std::fabs(sample));
  return peak;
}

static float window_rms(const std::vector<float> &mono, uint32_t start, uint32_t end)
{
  double sum_squares = 0.0;
  uint32_t count = 0U;
  for (uint32_t sampleIndex = start; sampleIndex < end && sampleIndex < mono.size(); ++sampleIndex)
  {
    const double sample = mono[sampleIndex];
    sum_squares += sample * sample;
    ++count;
  }
  return count > 0U ? static_cast<float>(std::sqrt(sum_squares / static_cast<double>(count))) : 0.f;
}

static float spectral_brightness(const std::vector<float> &mono, uint32_t sample_rate)
{
  float low = 0.f;
  float high_energy = 0.f;
  float low_energy = 0.f;
  const float coeff = 0.05f;
  const uint32_t start = sample_rate / 50U;
  const uint32_t end = std::min<uint32_t>(mono.size(), sample_rate / 2U);
  for (uint32_t sampleIndex = start; sampleIndex < end; ++sampleIndex)
  {
    const float sample = mono[sampleIndex];
    low += coeff * (sample - low);
    const float high = sample - low;
    low_energy += low * low;
    high_energy += high * high;
  }
  if (low_energy < 1.0e-12f)
    return 0.f;
  return high_energy / low_energy;
}

int main()
{
  // Pitch extremes must match R478+VR30 around panel mid (−6.12 / +9.54 st).
  {
    Ride909 low;
    Ride909 mid;
    Ride909 high;
    low.init(nullptr);
    mid.init(nullptr);
    high.init(nullptr);
    low.setParameter(Ride909::PITCH, 0);
    mid.setParameter(Ride909::PITCH, 512);
    high.setParameter(Ride909::PITCH, 1023);
    const float low_ratio = low.debugClockRatio();
    const float mid_ratio = mid.debugClockRatio();
    const float high_ratio = high.debugClockRatio();
    const float expected_low = Ride909::kTuneRMidOhms / (Ride909::kTuneRFixedOhms + Ride909::kTuneRPotOhms);
    const float expected_high = Ride909::kTuneRMidOhms / Ride909::kTuneRFixedOhms;
    std::printf("pitch_ratio low=%.4f mid=%.4f high=%.4f expected_low=%.4f expected_high=%.4f\n",
                low_ratio, mid_ratio, high_ratio, expected_low, expected_high);
    if (std::fabs(mid_ratio - 1.f) > 0.02f)
      return 1;
    if (std::fabs(low_ratio - expected_low) > 0.02f)
      return 2;
    if (std::fabs(high_ratio - expected_high) > 0.05f)
      return 3;
    // Low end must stay near −6.1 st, not the old symmetric −7.8.
    const float low_st = 12.f * std::log2(low_ratio);
    if (low_st < -6.5f || low_st > -5.7f)
      return 4;
    const float high_st = 12.f * std::log2(high_ratio);
    if (high_st < 9.2f || high_st > 9.8f)
      return 5;
  }

  std::vector<float> dark_mono;
  std::vector<float> bright_mono;
  std::vector<float> full_dec_mono;
  std::vector<float> short_dec_mono;

  {
    Ride909 dark;
    Ride909 bright;
    render_one_hit(dark, 512, 0, 1023, dark_mono);
    render_one_hit(bright, 512, 1023, 1023, bright_mono);
    const float dark_bright = spectral_brightness(dark_mono, 48000U);
    const float bright_bright = spectral_brightness(bright_mono, 48000U);
    std::printf("tone dark_ratio=%.4f bright_ratio=%.4f dark_lpf=%.3f bright_lpf=%.3f\n", dark_bright,
                bright_bright, dark.debugLpfACoeff(), bright.debugLpfACoeff());
    if (!(bright.debugLpfACoeff() > dark.debugLpfACoeff() + 0.15f))
      return 6;
    if (!(bright_bright > dark_bright * 1.05f))
      return 7;
  }

  {
    Ride909 full_dec;
    Ride909 short_dec;
    render_one_hit(full_dec, 512, 512, 1023, full_dec_mono);
    render_one_hit(short_dec, 512, 512, 0, short_dec_mono);
    const float full_late = window_rms(full_dec_mono, 24000U, 36000U);
    const float short_late = window_rms(short_dec_mono, 24000U, 36000U);
    const float full_peak = peak_of(full_dec_mono);
    const float short_peak = peak_of(short_dec_mono);
    std::printf("dec full_late=%.6f short_late=%.6f full_peak=%.4f short_peak=%.4f tau_full=%.3f tau_short=%.3f\n",
                full_late, short_late, full_peak, short_peak, full_dec.debugDecayTauSeconds(),
                short_dec.debugDecayTauSeconds());
    if (full_peak < 0.02f || short_peak < 0.01f)
      return 8;
    if (!(short_late < full_late * 0.35f))
      return 9;
    if (!(short_dec.debugDecayTauSeconds() < 0.1f))
      return 10;
    if (!(full_dec.debugDecayTauSeconds() > 1.5f))
      return 11;
  }

  {
    Ride909 unity;
    Ride909 boosted;
    std::vector<float> unity_mono;
    std::vector<float> boost_mono;
    render_one_hit(unity, 512, 512, 1023, unity_mono);
    boosted.init(nullptr);
    boosted.setParameter(Ride909::MIX, 1000);
    boosted.setParameter(Ride909::PUMP, 0);
    boosted.setParameter(Ride909::PITCH, 512);
    boosted.setParameter(Ride909::TONE, 512);
    boosted.setParameter(Ride909::DEC, 1023);
    boosted.setParameter(Ride909::GAIN, 1023);
    boosted.setTempo(21.f);
    boosted.touchEvent(0, k_unit_touch_phase_began, 512, 0);
    boosted.tempo4ppqnTick(1U);
    boosted.tempo4ppqnTick(2U);
    boosted.tempo4ppqnTick(3U);
    constexpr uint32_t kBlockSize = 128U;
    constexpr uint32_t kBlockCount = 500U;
    std::vector<float> block(kBlockSize * 2U, 0.f);
    boost_mono.assign(kBlockSize * kBlockCount, 0.f);
    for (uint32_t blockIndex = 0; blockIndex < kBlockCount; ++blockIndex)
    {
      std::fill(block.begin(), block.end(), 0.f);
      boosted.process(block.data(), block.data(), kBlockSize);
      for (uint32_t sampleIndex = 0; sampleIndex < kBlockSize; ++sampleIndex)
        boost_mono[blockIndex * kBlockSize + sampleIndex] = block[sampleIndex * 2U];
    }
    const float unity_peak = peak_of(unity_mono);
    const float boost_peak = peak_of(boost_mono);
    std::printf("gain unity_peak=%.4f boost_peak=%.4f gain_mul=%.3f\n", unity_peak, boost_peak,
                boosted.debugGainMul());
    if (std::fabs(boosted.debugGainMul() - 4.f) > 0.05f)
      return 12;
    if (!(boost_peak > unity_peak * 1.6f))
      return 13;
  }

  {
    Ride909 pump;
    pump.init(nullptr);
    pump.setParameter(Ride909::PUMP, 1023);
    const float floor = pump.debugPumpFloor();
    std::printf("pump_floor_at_max=%.4f\n", floor);
    if (floor > 0.03f)
      return 14;
  }

  std::printf("ride909_params_ok=1\n");
  return 0;
}
