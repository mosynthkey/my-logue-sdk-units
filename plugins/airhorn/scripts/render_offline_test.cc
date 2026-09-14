#include "airhorn_engine.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

static bool approxEqual(float a, float b, float tol)
{
  return std::fabs(a - b) <= tol;
}

int main()
{
  AirHornEngine engine;
  engine.init();
  engine.setTrackFromParam(false);
  engine.setParameter(AirHornEngine::LEVEL, 127);
  engine.setParameter(AirHornEngine::PMODE, 0); // Fixed

  engine.startVoice(127, 60); // Fixed mode ignores note for pitch

  float peak = 0.f;
  double sum_squares = 0.0;
  uint64_t sample_count = 0U;
  float prev = 0.f;
  float max_delta = 0.f;

  for (uint32_t sampleIndex = 0; sampleIndex < 48000U * 2U; ++sampleIndex)
  {
    const float sample = engine.renderMono();
    const float magnitude = std::fabs(sample);
    if (magnitude > peak)
      peak = magnitude;
    const float delta = std::fabs(sample - prev);
    if (sampleIndex > 64U && delta > max_delta)
      max_delta = delta;
    sum_squares += static_cast<double>(sample) * static_cast<double>(sample);
    ++sample_count;
    prev = sample;
  }

  if (peak < 0.1f)
  {
    std::printf("FAIL: fixed sustain peak too low (%.6f)\n", peak);
    return 1;
  }

  engine.releaseAll();
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U / 5U; ++sampleIndex)
    (void)engine.renderMono();

  // Key tracking uses the measured native root (~62.49), not assumed D# / retuned D4.
  const float expected = AirHornEngine::noteTransposeFor(60.f);
  const float want = std::pow(2.f, (60.f - kAirhornRootMidi) / 12.f);
  if (!approxEqual(expected, want, 0.01f))
  {
    std::printf("FAIL: note transpose got %.6f want %.6f (root=%.6f)\n",
                expected, want, kAirhornRootMidi);
    return 1;
  }
  // Fixed stays native (~D4+49c), so Key D4 is ~49 cents below Fixed.
  const float d4_xpose = AirHornEngine::noteTransposeFor(62.f);
  if (!(d4_xpose < 1.f && d4_xpose > 0.96f))
  {
    std::printf("FAIL: D4 transpose expected ~0.972, got %.6f\n", d4_xpose);
    return 1;
  }
  // Concert D#4 is ~51 cents above Fixed.
  const float ds4_xpose = AirHornEngine::noteTransposeFor(63.f);
  if (!(ds4_xpose > 1.f && ds4_xpose < 1.04f))
  {
    std::printf("FAIL: D#4 transpose expected ~1.030, got %.6f\n", ds4_xpose);
    return 1;
  }
  // Root MIDI must match settled Hz (A4=440), with Fixed left untuned.
  const float root_from_hz = 69.f + 12.f * std::log2(kAirhornSettledHz / 440.f);
  if (!approxEqual(root_from_hz, kAirhornRootMidi, 0.01f))
  {
    std::printf("FAIL: root midi %.6f != settled-derived %.6f\n",
                kAirhornRootMidi, root_from_hz);
    return 1;
  }

  AirHornEngine nts3;
  nts3.init();
  nts3.setTrackFromParam(true);
  nts3.setParameter(AirHornEngine::LEVEL, 1023);
  nts3.setParameter(AirHornEngine::DECAY, 127);
  nts3.setParameter(AirHornEngine::MIX, 1000);
  nts3.setParameter(AirHornEngine::PMODE_NTS3, 1);
  nts3.setParameter(AirHornEngine::PITCH, 768); // +1 oct (512 + 256)

  const char *sustain = nts3.getParameterStrValue(AirHornEngine::DECAY, 127);
  if (sustain == nullptr || std::strcmp(sustain, "Sustain") != 0)
  {
    std::printf("FAIL: expected Sustain label at decay 127\n");
    return 1;
  }
  const char *pitch_mode = nts3.getParameterStrValue(AirHornEngine::PMODE_NTS3, 1);
  if (pitch_mode == nullptr || std::strcmp(pitch_mode, "Pitch") != 0)
  {
    std::printf("FAIL: expected Pitch mode label\n");
    return 1;
  }
  // Allow smoothing to settle toward +1 octave (ratio 2).
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U / 5U; ++sampleIndex)
    (void)nts3.renderMono();
  if (!approxEqual(nts3.pitchTranspose(), 2.f, 0.05f))
  {
    std::printf("FAIL: +1 oct transpose got %.6f\n", nts3.pitchTranspose());
    return 1;
  }

  // Continuous sweep endpoints: 0 → 0.25, 1023 → ~4.
  nts3.setParameter(AirHornEngine::PITCH, 0);
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U / 5U; ++sampleIndex)
    (void)nts3.renderMono();
  if (!approxEqual(nts3.pitchTranspose(), 0.25f, 0.02f))
  {
    std::printf("FAIL: -2 oct transpose got %.6f\n", nts3.pitchTranspose());
    return 1;
  }
  nts3.setParameter(AirHornEngine::PITCH, 1023);
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U / 5U; ++sampleIndex)
    (void)nts3.renderMono();
  if (!approxEqual(nts3.pitchTranspose(), 4.f, 0.05f))
  {
    std::printf("FAIL: +2 oct transpose got %.6f\n", nts3.pitchTranspose());
    return 1;
  }

  AirHornEngine fade_engine;
  fade_engine.init();
  fade_engine.setTrackFromParam(true);
  fade_engine.setParameter(AirHornEngine::LEVEL, 1023);
  fade_engine.setParameter(AirHornEngine::DECAY, 0);
  fade_engine.setParameter(AirHornEngine::MIX, 1000);
  fade_engine.startVoice(127, 0);
  float late_peak = 0.f;
  for (uint32_t sampleIndex = 0; sampleIndex < 48000U * 3U; ++sampleIndex)
  {
    const float sample = fade_engine.renderMono();
    if (sampleIndex > 48000U * 2U)
    {
      const float magnitude = std::fabs(sample);
      if (magnitude > late_peak)
        late_peak = magnitude;
    }
  }
  if (late_peak > 0.02f)
  {
    std::printf("FAIL: short decay still loud at 2-3s (%.6f)\n", late_peak);
    return 1;
  }

  const double rms = std::sqrt(sum_squares / static_cast<double>(sample_count));
  std::printf("peak=%.6f rms=%.6f max_delta=%.6f loop_samples=%u key_xpose=%.4f\n",
              peak, rms, max_delta, kAirhornSamples[0].length, expected);
  if (rms < 0.02f)
    return 1;
  return 0;
}
