#include "dubthrow.h"
#include "runtime.h"

#include <cmath>
#include <cstdio>
#include <vector>

static float windowPeak(const std::vector<float> &mono, uint32_t start_sample, uint32_t count)
{
  float peak = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < count; ++sampleIndex)
  {
    const uint32_t index = start_sample + sampleIndex;
    if (index >= mono.size())
      break;
    const float sample = mono[index] < 0.f ? -mono[index] : mono[index];
    if (sample > peak)
      peak = sample;
  }
  return peak;
}

static void render(DubThrow &fx, const float *left, const float *right, uint32_t frames,
                   bool touching, std::vector<float> &mono_out)
{
  if (touching)
    fx.touchEvent(0, k_unit_touch_phase_began, 1023U, 512U);
  else
    fx.touchEvent(0, k_unit_touch_phase_ended, 0U, 0U);

  const uint32_t block_frames = 64U;
  std::vector<float> in_block(block_frames * 2U, 0.f);
  std::vector<float> out_block(block_frames * 2U, 0.f);
  uint32_t frameOffset = 0U;
  while (frameOffset < frames)
  {
    const uint32_t this_block =
        (frames - frameOffset) > block_frames ? block_frames : (frames - frameOffset);
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

int main()
{
  DubThrow fx;
  std::vector<float> ram(fx.getBufferSize(), 0.f);
  fx.init(ram.data());
  fx.setTempo(120.f);
  fx.setParameter(DubThrow::THROW, 1023);
  fx.setParameter(DubThrow::TONE, 563);
  fx.setParameter(DubThrow::DEPTH, 1000);
  fx.setParameter(DubThrow::TIME, DubThrow::TIME_8D);
  fx.setParameter(DubThrow::FDBK, 700);
  fx.setParameter(DubThrow::SPRD, 358);
  fx.setParameter(DubThrow::MODE, DubThrow::MODE_PPONG);
  fx.setParameter(DubThrow::TOUCH, DubThrow::TOUCH_GATE);

  const uint32_t burst_frames = 4800U;   // 100 ms click/tone burst
  const uint32_t silence_frames = 96000U; // 2 s for dotted-eighth tails @ 120
  std::vector<float> burst_left(burst_frames, 0.f);
  std::vector<float> burst_right(burst_frames, 0.f);
  for (uint32_t sampleIndex = 0; sampleIndex < burst_frames; ++sampleIndex)
  {
    const float env = sampleIndex < 48U ? sampleIndex / 48.f : 1.f;
    const float sample = env * 0.5f * sinf(6.28318530718f * 220.f * sampleIndex / 48000.f);
    burst_left[sampleIndex] = sample;
    burst_right[sampleIndex] = sample * 0.9f;
  }
  std::vector<float> silent_left(silence_frames, 0.f);
  std::vector<float> silent_right(silence_frames, 0.f);

  std::vector<float> mono;
  render(fx, burst_left.data(), burst_right.data(), burst_frames, true, mono);
  render(fx, silent_left.data(), silent_right.data(), silence_frames, false, mono);

  // Dotted 1/8 @ 120 BPM = 0.75 beat = 375 ms = 18000 samples.
  const float peak_first = windowPeak(mono, 18000U, 2000U);
  const float peak_second = windowPeak(mono, 36000U, 2000U);
  const float peak_late = windowPeak(mono, 80000U, 2000U);
  const float dry_region = windowPeak(mono, 100U, 1000U);

  std::printf("dry_burst_peak=%.4f first_echo=%.4f second_echo=%.4f late=%.4f\n", dry_region,
              peak_first, peak_second, peak_late);

  if (dry_region < 0.1f)
  {
    std::printf("FAIL: dry path too quiet\n");
    return 1;
  }
  if (peak_first < 0.02f)
  {
    std::printf("FAIL: first echo missing (send/gate or delay broken)\n");
    return 1;
  }
  if (peak_second < 0.005f)
  {
    std::printf("FAIL: feedback tail too short\n");
    return 1;
  }
  if (peak_late > peak_first)
  {
    std::printf("FAIL: limiter/feedback runaway\n");
    return 1;
  }

  // High FDBK must bloom without parking on softclip rails.
  // Dark tone previously railed by echo 2–3 (peaks ≥ 0.72).
  DubThrow hot;
  std::vector<float> hot_ram(hot.getBufferSize(), 0.f);
  hot.init(hot_ram.data());
  hot.setTempo(120.f);
  hot.setParameter(DubThrow::THROW, 1023);
  hot.setParameter(DubThrow::TONE, 200);
  hot.setParameter(DubThrow::DEPTH, 1000);
  hot.setParameter(DubThrow::TIME, DubThrow::TIME_8);
  hot.setParameter(DubThrow::FDBK, 1023);
  hot.setParameter(DubThrow::SPRD, 0);
  hot.setParameter(DubThrow::MODE, DubThrow::MODE_DUAL);
  hot.setParameter(DubThrow::TOUCH, DubThrow::TOUCH_ALWAYS);

  const uint32_t delay_samples = 12000U; // 1/8 @ 120 BPM
  const uint32_t hot_frames = delay_samples * 6U;
  std::vector<float> hot_left(hot_frames, 0.f);
  std::vector<float> hot_right(hot_frames, 0.f);
  for (uint32_t sampleIndex = 0; sampleIndex < delay_samples / 2U; ++sampleIndex)
  {
    const float sample = 0.5f * sinf(6.28318530718f * 440.f * sampleIndex / 48000.f);
    hot_left[sampleIndex] = sample;
    hot_right[sampleIndex] = sample;
  }
  std::vector<float> hot_mono;
  render(hot, hot_left.data(), hot_right.data(), hot_frames, true, hot_mono);

  const float peak_echo2 = windowPeak(hot_mono, delay_samples * 2U, delay_samples);
  const float peak_echo3 = windowPeak(hot_mono, delay_samples * 3U, delay_samples);

  std::printf("high_fdbk_dark echo2=%.4f echo3=%.4f\n", peak_echo2, peak_echo3);

  if (peak_echo2 > 0.70f || peak_echo3 > 0.72f)
  {
    std::printf("FAIL: high feedback saturated within a few echoes\n");
    return 1;
  }

  // Mid tone + max FDBK: crest must stay musical after several recirculations.
  DubThrow mid;
  std::vector<float> mid_ram(mid.getBufferSize(), 0.f);
  mid.init(mid_ram.data());
  mid.setTempo(120.f);
  mid.setParameter(DubThrow::THROW, 1023);
  mid.setParameter(DubThrow::TONE, 563);
  mid.setParameter(DubThrow::DEPTH, 1000);
  mid.setParameter(DubThrow::TIME, DubThrow::TIME_8);
  mid.setParameter(DubThrow::FDBK, 1023);
  mid.setParameter(DubThrow::SPRD, 0);
  mid.setParameter(DubThrow::MODE, DubThrow::MODE_DUAL);
  mid.setParameter(DubThrow::TOUCH, DubThrow::TOUCH_ALWAYS);

  const uint32_t mid_frames = delay_samples * 8U;
  std::vector<float> mid_left(mid_frames, 0.f);
  std::vector<float> mid_right(mid_frames, 0.f);
  for (uint32_t sampleIndex = 0; sampleIndex < delay_samples / 2U; ++sampleIndex)
  {
    const float sample = 0.5f * sinf(6.28318530718f * 440.f * sampleIndex / 48000.f);
    mid_left[sampleIndex] = sample;
    mid_right[sampleIndex] = sample;
  }
  std::vector<float> mid_mono;
  render(mid, mid_left.data(), mid_right.data(), mid_frames, true, mid_mono);

  float peak_echo6 = 0.f;
  float sum2_echo6 = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < delay_samples; ++sampleIndex)
  {
    const float sample = mid_mono[delay_samples * 6U + sampleIndex];
    const float abs_sample = sample < 0.f ? -sample : sample;
    if (abs_sample > peak_echo6)
      peak_echo6 = abs_sample;
    sum2_echo6 += sample * sample;
  }
  const float rms_echo6 = std::sqrt(sum2_echo6 / static_cast<float>(delay_samples));
  const float crest_echo6 = peak_echo6 / (rms_echo6 + 1e-9f);

  std::printf("high_fdbk_mid echo6=%.4f crest6=%.2f\n", peak_echo6, crest_echo6);

  if (crest_echo6 < 2.5f)
  {
    std::printf("FAIL: high feedback crest collapsed (softclip rail lock)\n");
    return 1;
  }

  std::printf("OK\n");
  return 0;
}
