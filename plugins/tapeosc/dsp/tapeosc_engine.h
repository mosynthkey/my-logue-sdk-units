#pragma once

/*
 * File: tapeosc_engine.h
 *
 * Tape-style varispeed oscillator. A rolling waveform buffer is written at
 * target pitch while the read head follows playback_rate, producing tape
 * motor start/stop pitch sweeps instead of a simple pitch envelope.
 *
 * The source is one logue mipmapped oscillator (osc_bl2_sawf / sqrf / parf
 * or osc_sinf). NTS-1 mkII and microKORG2 both export those tables.
 *
 */

#include "osc_api.h"
#include "macros.h"
#include "utils/float_math.h"
#include <math.h>
#include <stdint.h>

template <bool Quantize>
struct TapeSampleOps;

template <>
struct TapeSampleOps<false>
{
  typedef float Sample;

  static void store(Sample &slot, float sample) { slot = sample; }

  static float load(Sample sample) { return sample; }
};

template <>
struct TapeSampleOps<true>
{
  typedef int16_t Sample;

  static void store(Sample &slot, float sample)
  {
    if (sample > 1.f)
      sample = 1.f;
    else if (sample < -1.f)
      sample = -1.f;
    slot = static_cast<int16_t>(sample * 32767.f);
  }

  static float load(Sample sample) { return static_cast<float>(sample) * (1.f / 32767.f); }
};

template <uint32_t BufferSize = 4096U, bool QuantizeBuffer = false>
class TapeOscEngine
{
public:
  static const uint32_t kBufferSize = BufferSize;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kOutputTrim = 0.62f;
  static constexpr float kMinLpfHz = 180.f;
  static constexpr float kMaxLpfHz = 14000.f;
  static constexpr float kMinWearLpfHz = 2200.f;
  static constexpr float kWowHz = 0.55f;
  static constexpr float kFlutterHz = 6.5f;
  // Former full-scale wow was ±100% of playback rate. Keep one tenth of that.
  static constexpr float kWowDepth = 0.1f;

  enum Waveform : uint8_t
  {
    WAVEFORM_SAW = 0U,
    WAVEFORM_SQUARE,
    WAVEFORM_TRIANGLE,
    WAVEFORM_SINE,
    NUM_WAVEFORMS
  };

  enum
  {
    kWaveform = 0U,
    kStart,
    kStop,
    kWear,
    kWow,
    kNumParams
  };

  enum class TransportState : uint8_t
  {
    Idle = 0U,
    Starting,
    Running,
    Stopping
  };

  struct Params
  {
    Waveform waveform = WAVEFORM_SAW;
    float start_sec = 0.093f;
    float stop_sec = 0.558f;
    float wear = 0.f;
    float wow = 0.f;
  };

  void setDefaults()
  {
    Params params;
    params.waveform = WAVEFORM_SAW;
    params.start_sec = 0.093f;
    params.stop_sec = 0.558f;
    params.wear = 0.f;
    params.wow = 0.f;
    setParams(params);
  }

  void reset()
  {
    clearBuffer();
    write_pos_ = 0.f;
    read_pos_ = 0.f;
    playback_rate_ = 0.f;
    lpf_state_ = 0.f;
    wear_lpf_state_ = 0.f;
    wow_phase_ = 0.f;
    flutter_phase_ = 0.f;
    wow_mix_ = 0.f;
    hold_counter_ = 0.f;
    held_sample_ = 0.f;
    phase_ = 0.f;
    transport_state_ = TransportState::Idle;
    active_ = false;
  }

  void randomizePhase()
  {
    float phase = osc_white();
    phase -= floorf(phase);
    phase_ = phase;
  }

  void applyParam(uint8_t index, int32_t value)
  {
    Params params = params_;

    switch (index)
    {
    case kWaveform:
    {
      uint32_t waveform = static_cast<uint32_t>(value);
      if (waveform >= NUM_WAVEFORMS)
        waveform = NUM_WAVEFORMS - 1U;
      params.waveform = static_cast<Waveform>(waveform);
      break;
    }
    case kStart:
      params.start_sec = millisecondsToSeconds(value);
      break;
    case kStop:
      params.stop_sec = millisecondsToSeconds(value);
      break;
    case kWear:
      params.wear = param_10bit_to_f32(value);
      break;
    case kWow:
    {
      float wow = static_cast<float>(value) * 0.01f;
      if (wow < 0.f)
        wow = 0.f;
      if (wow > 1.f)
        wow = 1.f;
      params.wow = wow;
      break;
    }
    default:
      return;
    }

    setParams(params);
  }

  const char *parameterString(uint8_t index, int32_t value) const
  {
    static const char *waveform_names[NUM_WAVEFORMS] = {
        "SAW",
        "SQR",
        "TRI",
        "SINE",
    };

    if (index == kWaveform && value >= 0 && value < static_cast<int32_t>(NUM_WAVEFORMS))
      return waveform_names[value];

    return nullptr;
  }

  void setParams(const Params &params)
  {
    params_ = params;
    updateTransportCoeffs();
    updateFilterCoeffs();
  }

  const Params &getParams() const { return params_; }

  void setPitch(float w0, float note)
  {
    base_w0_ = w0;
    base_note_ = note;
    updateBandLimit();
  }

  void beginStart()
  {
    clearBuffer();
    write_pos_ = 0.f;
    read_pos_ = 0.f;
    playback_rate_ = 0.f;
    lpf_state_ = 0.f;
    wear_lpf_state_ = 0.f;
    hold_counter_ = 0.f;
    held_sample_ = 0.f;
    transport_state_ = TransportState::Starting;
    active_ = true;
  }

  void beginStop()
  {
    if (!active_ || transport_state_ == TransportState::Idle ||
        transport_state_ == TransportState::Stopping)
      return;

    transport_state_ = TransportState::Stopping;
  }

  float render()
  {
    if (!active_ && transport_state_ == TransportState::Idle)
      return 0.f;

    const float source_sample = renderSource();
    const uint32_t write_index = static_cast<uint32_t>(write_pos_);
    SampleOps::store(buffer_[write_index], source_sample);
    write_pos_ += 1.f;
    if (write_pos_ >= static_cast<float>(kBufferSize))
      write_pos_ -= static_cast<float>(kBufferSize);

    advanceTransport();

    float effective_rate = playback_rate_;
    if (params_.wow > 0.f)
    {
      advanceWowFlutter();
      const float wow_scale = params_.wow * kWowDepth * playback_rate_;
      effective_rate = playback_rate_ * (1.f + wow_scale * wow_mix_);
      if (effective_rate < 0.f)
        effective_rate = 0.f;
    }

    const float raw_sample = readBuffer(read_pos_);
    read_pos_ += effective_rate;
    if (read_pos_ >= static_cast<float>(kBufferSize))
      read_pos_ -= static_cast<float>(kBufferSize);

    // Endpoint coeffs are exact; the sweep between them avoids a per-sample expf.
    const float lpf_coeff = lpf_coeff_min_ + playback_rate_ * (lpf_coeff_max_ - lpf_coeff_min_);
    lpf_state_ += lpf_coeff * (raw_sample - lpf_state_);

    const float motor_gain = 0.15f + 0.85f * sqrtf(fmaxf(playback_rate_, 0.f));
    float output = lpf_state_ * motor_gain;
    output = applyWear(output);
    return output * kOutputTrim;
  }

  bool isActive() const { return active_; }

  TransportState transportState() const { return transport_state_; }

  float playbackRate() const { return playback_rate_; }

private:
  typedef TapeSampleOps<QuantizeBuffer> SampleOps;

  static float millisecondsToSeconds(int32_t value)
  {
    float milliseconds = static_cast<float>(value);
    if (milliseconds < 1.f)
      milliseconds = 1.f;
    return milliseconds * 0.001f;
  }

  // osc_bl2_* always reads mip idx and idx+1, so the index stays below the last table.
  static float bandLimitIndex(float note, const uint8_t *notes, uint32_t noteCount)
  {
    uint32_t index = 0U;
    const uint32_t lastIndex = noteCount - 1U;
    while (index < lastIndex && static_cast<float>(notes[index]) < note)
      ++index;

    const uint8_t previous = index > 0U ? notes[index - 1U] : 0U;
    const float interval = static_cast<float>(notes[index] - previous);
    float fractional = static_cast<float>(index);
    if (interval > 0.f)
      fractional += (note - static_cast<float>(previous)) / interval;

    const float limit = static_cast<float>(lastIndex) - 0.001f;
    if (fractional < 0.f)
      return 0.f;
    if (fractional > limit)
      return limit;
    return fractional;
  }

  void clearBuffer()
  {
    for (uint32_t sampleIndex = 0; sampleIndex < kBufferSize; ++sampleIndex)
      buffer_[sampleIndex] = 0;
  }

  void updateTransportCoeffs()
  {
    const float start_sec = (params_.start_sec < 0.001f) ? 0.001f : params_.start_sec;
    const float stop_sec = (params_.stop_sec < 0.001f) ? 0.001f : params_.stop_sec;
    start_coeff_ = 1.f - expf(-1.f / (start_sec * getSampleRate()));
    stop_coeff_ = 1.f - expf(-1.f / (stop_sec * getSampleRate()));
  }

  static float getSampleRate() { return static_cast<float>(k_samplerate); }

  void updateBandLimit()
  {
    saw_idx_ = bandLimitIndex(base_note_, wt_saw_notes, k_wt_saw_notes_cnt);
    sqr_idx_ = bandLimitIndex(base_note_, wt_sqr_notes, k_wt_sqr_notes_cnt);
    par_idx_ = bandLimitIndex(base_note_, wt_par_notes, k_wt_par_notes_cnt);
  }

  float renderSource()
  {
    float sample = 0.f;
    switch (params_.waveform)
    {
    case WAVEFORM_SQUARE:
      sample = osc_bl2_sqrf(phase_, sqr_idx_);
      break;
    case WAVEFORM_TRIANGLE:
      sample = osc_bl2_parf(phase_, par_idx_);
      break;
    case WAVEFORM_SINE:
      sample = osc_sinf(phase_);
      break;
    case WAVEFORM_SAW:
    default:
      sample = osc_bl2_sawf(phase_, saw_idx_);
      break;
    }

    phase_ += base_w0_;
    if (phase_ >= 1.f)
      phase_ -= 1.f;
    return sample;
  }

  float readBuffer(float position) const
  {
    if (position >= static_cast<float>(kBufferSize))
      position -= static_cast<float>(kBufferSize);
    if (position < 0.f)
      position = 0.f;

    const uint32_t index_a = static_cast<uint32_t>(position);
    uint32_t index_b = index_a + 1U;
    if (index_b >= kBufferSize)
      index_b = 0U;
    const float frac = position - static_cast<float>(index_a);
    return linintf(frac, SampleOps::load(buffer_[index_a]), SampleOps::load(buffer_[index_b]));
  }

  void advanceTransport()
  {
    switch (transport_state_)
    {
    case TransportState::Starting:
      playback_rate_ += (1.f - playback_rate_) * start_coeff_;
      if (playback_rate_ > 0.9995f)
      {
        playback_rate_ = 1.f;
        transport_state_ = TransportState::Running;
      }
      break;

    case TransportState::Running:
      playback_rate_ = 1.f;
      break;

    case TransportState::Stopping:
      playback_rate_ += (0.f - playback_rate_) * stop_coeff_;
      if (playback_rate_ < 0.00005f)
      {
        playback_rate_ = 0.f;
        transport_state_ = TransportState::Idle;
        active_ = false;
      }
      break;

    case TransportState::Idle:
    default:
      playback_rate_ = 0.f;
      break;
    }
  }

  void updateFilterCoeffs()
  {
    const float sample_rate = getSampleRate();
    lpf_coeff_min_ = 1.f - expf((-kTwoPi * kMinLpfHz) / sample_rate);
    lpf_coeff_max_ = 1.f - expf((-kTwoPi * kMaxLpfHz) / sample_rate);

    const float wear_hz = kMaxLpfHz - params_.wear * (kMaxLpfHz - kMinWearLpfHz);
    wear_lpf_coeff_ = 1.f - expf((-kTwoPi * wear_hz) / sample_rate);
  }

  void advanceWowFlutter()
  {
    const float sample_rate = getSampleRate();
    wow_phase_ += kWowHz / sample_rate;
    flutter_phase_ += kFlutterHz / sample_rate;
    if (wow_phase_ >= 1.f)
      wow_phase_ -= 1.f;
    if (flutter_phase_ >= 1.f)
      flutter_phase_ -= 1.f;

    const float wow_lfo = osc_sinf(wow_phase_);
    const float flutter_lfo = osc_sinf(flutter_phase_);
    wow_mix_ = wow_lfo * 0.72f + flutter_lfo * 0.28f;
  }

  static float saturateWear(float sample, float wear)
  {
    const float drive = 1.f + wear * 3.2f;
    const float driven = sample * drive;
    const float abs_sample = fabsf(driven);
    if (abs_sample < 1.f)
      return driven;
    return driven / (1.f + abs_sample - 1.f);
  }

  float applyWear(float sample)
  {
    const float wear = params_.wear;
    if (wear <= 0.f)
      return sample;

    wear_lpf_state_ += wear_lpf_coeff_ * (sample - wear_lpf_state_);
    float output = linintf(wear, sample, wear_lpf_state_);

    output = saturateWear(output, wear);

    const float hold_stride = 1.f + wear * wear * 64.f;
    hold_counter_ += 1.f;
    if (hold_counter_ >= hold_stride)
    {
      hold_counter_ -= hold_stride;
      held_sample_ = output;
    }
    output = linintf(wear * 0.55f, output, held_sample_);

    const float hiss = osc_white() * wear * 0.09f;
    output += hiss;

    if (wear > 0.35f && osc_white() > (1.f - wear * 0.015f))
      output *= 0.2f;

    return output;
  }

  Params params_;
  typename SampleOps::Sample buffer_[kBufferSize] = {};
  float write_pos_ = 0.f;
  float read_pos_ = 0.f;
  float playback_rate_ = 0.f;
  float start_coeff_ = 0.f;
  float stop_coeff_ = 0.f;
  float base_w0_ = 0.f;
  float base_note_ = 60.f;
  float phase_ = 0.f;
  float saw_idx_ = 0.f;
  float sqr_idx_ = 0.f;
  float par_idx_ = 0.f;
  float lpf_state_ = 0.f;
  float lpf_coeff_min_ = 0.f;
  float lpf_coeff_max_ = 0.f;
  float wear_lpf_state_ = 0.f;
  float wear_lpf_coeff_ = 0.f;
  float wow_phase_ = 0.f;
  float flutter_phase_ = 0.f;
  float wow_mix_ = 0.f;
  float hold_counter_ = 0.f;
  float held_sample_ = 0.f;
  TransportState transport_state_ = TransportState::Idle;
  bool active_ = false;
};
