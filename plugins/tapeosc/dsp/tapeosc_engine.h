#pragma once

/*
 * File: tapeosc_engine.h
 *
 * Tape-style varispeed oscillator. A rolling waveform buffer is written at
 * target pitch while the read head follows playback_rate, producing tape
 * motor start/stop pitch sweeps instead of a simple pitch envelope.
 *
 * The source is the logue mipmapped oscillator set (osc_bl2_sawf / sqrf / parf
 * and osc_sinf). NTS-1 mkII and microKORG2 both export those tables, so the
 * saw does not need a separate PolyBLEP. Reading the tape slower, or mixing
 * toward zero-order hold with Grit, can still alias the recorded stream.
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
  static const uint32_t kUnisonCount = 8U;
  static constexpr float kTwoPi = 6.283185307179586f;
  static constexpr float kOutputTrim = 0.62f;
  static constexpr float kMinLpfHz = 180.f;
  static constexpr float kMaxLpfHz = 14000.f;
  static constexpr float kMinWearLpfHz = 2200.f;
  static constexpr float kWowHz = 0.55f;
  static constexpr float kFlutterHz = 6.5f;
  // Former full-scale wow was ±100% of playback rate. Keep one tenth of that.
  static constexpr float kWowDepth = 0.1f;
  static constexpr float kGainSmoothing = 0.002f;
  // 100% matches the former 0-1023 detune knob at raw value 300.
  static constexpr float kDetuneFullScale = 300.f / 1023.f;

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
    kGrit,
    kWear,
    kWow,
    kUnison,
    kDetune,
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
    float grit = 0.35f;
    float wear = 0.f;
    float wow = 0.f;
    float unison = 1.f;
    float detune = 0.f;
  };

  void setDefaults()
  {
    Params params;
    params.waveform = WAVEFORM_SAW;
    params.start_sec = 0.093f;
    params.stop_sec = 0.558f;
    params.grit = 358.f / 1023.f;
    params.wear = 0.f;
    params.wow = 0.f;
    params.unison = 1.f;
    params.detune = 0.f;
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
    transport_state_ = TransportState::Idle;
    active_ = false;
    for (uint32_t voiceIndex = 0; voiceIndex < kUnisonCount; ++voiceIndex)
      unison_phase_[voiceIndex] = 0.f;
    snapUnisonGains();
  }

  void randomizePhase()
  {
    for (uint32_t voiceIndex = 0; voiceIndex < kUnisonCount; ++voiceIndex)
    {
      float phase = osc_white();
      phase -= floorf(phase);
      unison_phase_[voiceIndex] = phase;
    }
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
    case kGrit:
      params.grit = param_10bit_to_f32(value);
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
    case kUnison:
    {
      int32_t count = value;
      if (count < 1)
        count = 1;
      if (count > static_cast<int32_t>(kUnisonCount))
        count = static_cast<int32_t>(kUnisonCount);
      params.unison = static_cast<float>(count);
      break;
    }
    case kDetune:
    {
      float detune = static_cast<float>(value) * 0.01f;
      if (detune < 0.f)
        detune = 0.f;
      if (detune > 1.f)
        detune = 1.f;
      params.detune = detune;
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
    updateWearCoeff();
    updateUnison();
  }

  const Params &getParams() const { return params_; }

  void setPitch(float w0, float note)
  {
    base_w0_ = w0;
    base_note_ = note;
    updateUnison();
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
    snapUnisonGains();
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

    float source_sample = renderUnison();
    // In-phase unison can peak above the band-limited table. Keep the tape in range.
    if (source_sample > 1.f)
      source_sample = 1.f;
    else if (source_sample < -1.f)
      source_sample = -1.f;
    const uint32_t write_index = static_cast<uint32_t>(write_pos_) % kBufferSize;
    SampleOps::store(buffer_[write_index], source_sample);
    write_pos_ += 1.f;
    if (write_pos_ >= static_cast<float>(kBufferSize))
      write_pos_ -= static_cast<float>(kBufferSize);

    advanceTransport();
    advanceWowFlutter();

    const float raw_sample = readBuffer(read_pos_);

    const float wow_scale = params_.wow * kWowDepth * playback_rate_;
    float effective_rate = playback_rate_ * (1.f + wow_scale * wow_mix_);
    if (effective_rate < 0.f)
      effective_rate = 0.f;
    read_pos_ += effective_rate;
    while (read_pos_ >= static_cast<float>(kBufferSize))
      read_pos_ -= static_cast<float>(kBufferSize);

    updateLpfCoeff();
    lpf_state_ += lpf_coeff_ * (raw_sample - lpf_state_);

    const float motor_gain = 0.15f + 0.85f * sqrtf(fmaxf(playback_rate_, 0.f));
    float output = lpf_state_ * motor_gain;

    output = applyWear(output);

    return output * kOutputTrim;
  }

  bool isActive() const { return active_; }

private:
  typedef TapeSampleOps<QuantizeBuffer> SampleOps;

  static float millisecondsToSeconds(int32_t value)
  {
    float milliseconds = static_cast<float>(value);
    if (milliseconds < 1.f)
      milliseconds = 1.f;
    return milliseconds * 0.001f;
  }

  static float spreadCurve(float spread_0_1)
  {
    const float clamped = (spread_0_1 < 0.f) ? 0.f : ((spread_0_1 > 1.f) ? 1.f : spread_0_1);
    static const float kSpreadLut[17] = {
        0.f, 0.00967268f, 0.0220363f, 0.0339636f, 0.0467636f, 0.0591273f, 0.0714909f,
        0.0838545f, 0.0967273f, 0.121527f, 0.147127f, 0.193455f, 0.243418f, 0.293382f,
        0.343345f, 0.3928f, 1.f};
    const float scaled = clamped * 16.f;
    const uint32_t lutIndex = static_cast<uint32_t>(scaled);
    const float frac = scaled - static_cast<float>(lutIndex);
    if (lutIndex >= 16U)
      return 1.f;
    return linintf(frac, kSpreadLut[lutIndex], kSpreadLut[lutIndex + 1U]);
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

  void snapUnisonGains()
  {
    unison_norm_ = unison_target_norm_;
    for (uint32_t voiceIndex = 0; voiceIndex < kUnisonCount; ++voiceIndex)
      unison_gain_[voiceIndex] = unison_target_gain_[voiceIndex];
  }

  void updateUnison()
  {
    const float spread_amount = spreadCurve(params_.detune * kDetuneFullScale);
    // HyperSaw order through the low outer voice. Count 8 omits the high +960 voice.
    static const float kDetuneCoeff[kUnisonCount] = {
        0.f,
        -128.f, 128.f,
        -408.f, 408.f,
        -704.f, 704.f,
        -960.f};

    const uint32_t active_count = static_cast<uint32_t>(params_.unison);
    float energy = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kUnisonCount; ++voiceIndex)
    {
      const float gain = (voiceIndex < active_count) ? 1.f : 0.f;
      unison_target_gain_[voiceIndex] = gain;
      energy += gain * gain;

      float detune_ratio = 1.f + (kDetuneCoeff[voiceIndex] * spread_amount) * (1.f / 720.f);
      if (detune_ratio < 0.05f)
        detune_ratio = 0.05f;
      unison_w0_[voiceIndex] = base_w0_ * detune_ratio;

      const float voice_note = base_note_ + 12.f * log2f(detune_ratio);
      saw_idx_[voiceIndex] = bandLimitIndex(voice_note, wt_saw_notes, k_wt_saw_notes_cnt);
      sqr_idx_[voiceIndex] = bandLimitIndex(voice_note, wt_sqr_notes, k_wt_sqr_notes_cnt);
      par_idx_[voiceIndex] = bandLimitIndex(voice_note, wt_par_notes, k_wt_par_notes_cnt);
    }

    if (energy < 1e-6f)
      unison_target_norm_ = 0.f;
    else
      unison_target_norm_ = 1.f / sqrtf(energy);
  }

  float renderWave(float phase, uint32_t voiceIndex) const
  {
    switch (params_.waveform)
    {
    case WAVEFORM_SQUARE:
      return osc_bl2_sqrf(phase, sqr_idx_[voiceIndex]);
    case WAVEFORM_TRIANGLE:
      return osc_bl2_parf(phase, par_idx_[voiceIndex]);
    case WAVEFORM_SINE:
      return osc_sinf(phase);
    case WAVEFORM_SAW:
    default:
      return osc_bl2_sawf(phase, saw_idx_[voiceIndex]);
    }
  }

  float renderUnison()
  {
    unison_norm_ += (unison_target_norm_ - unison_norm_) * kGainSmoothing;

    float sum = 0.f;
    for (uint32_t voiceIndex = 0; voiceIndex < kUnisonCount; ++voiceIndex)
    {
      float &smoothed_gain = unison_gain_[voiceIndex];
      const float target_gain = unison_target_gain_[voiceIndex];
      smoothed_gain += (target_gain - smoothed_gain) * kGainSmoothing;
      if (target_gain == 0.f && smoothed_gain < 1e-5f)
        smoothed_gain = 0.f;
      if (smoothed_gain <= 0.f)
        continue;

      sum += renderWave(unison_phase_[voiceIndex], voiceIndex) * smoothed_gain;

      float phase = unison_phase_[voiceIndex] + unison_w0_[voiceIndex];
      phase -= floorf(phase);
      unison_phase_[voiceIndex] = phase;
    }

    return sum * unison_norm_;
  }

  float readBuffer(float position) const
  {
    float wrapped = position;
    while (wrapped >= static_cast<float>(kBufferSize))
      wrapped -= static_cast<float>(kBufferSize);
    while (wrapped < 0.f)
      wrapped += static_cast<float>(kBufferSize);

    const uint32_t index_a = static_cast<uint32_t>(wrapped) % kBufferSize;
    const uint32_t index_b = (index_a + 1U) % kBufferSize;
    const float frac = wrapped - floorf(wrapped);
    const float linear_sample = linintf(frac, SampleOps::load(buffer_[index_a]), SampleOps::load(buffer_[index_b]));
    const float zoh_sample = SampleOps::load(buffer_[index_a]);
    return linintf(params_.grit, linear_sample, zoh_sample);
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

  void updateLpfCoeff()
  {
    const float cutoff_hz = kMinLpfHz + playback_rate_ * (kMaxLpfHz - kMinLpfHz);
    lpf_coeff_ = 1.f - expf((-kTwoPi * cutoff_hz) / getSampleRate());
  }

  void updateWearCoeff()
  {
    const float cutoff_hz = kMaxLpfHz - params_.wear * (kMaxLpfHz - kMinWearLpfHz);
    wear_lpf_coeff_ = 1.f - expf((-kTwoPi * cutoff_hz) / getSampleRate());
  }

  void advanceWowFlutter()
  {
    const float sample_rate = getSampleRate();
    wow_phase_ += kWowHz / sample_rate;
    flutter_phase_ += kFlutterHz / sample_rate;
    wow_phase_ -= floorf(wow_phase_);
    flutter_phase_ -= floorf(flutter_phase_);

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
  float lpf_state_ = 0.f;
  float lpf_coeff_ = 0.f;
  float wear_lpf_state_ = 0.f;
  float wear_lpf_coeff_ = 0.f;
  float wow_phase_ = 0.f;
  float flutter_phase_ = 0.f;
  float wow_mix_ = 0.f;
  float hold_counter_ = 0.f;
  float held_sample_ = 0.f;
  float unison_phase_[kUnisonCount] = {};
  float unison_w0_[kUnisonCount] = {};
  float unison_gain_[kUnisonCount] = {};
  float unison_target_gain_[kUnisonCount] = {};
  float saw_idx_[kUnisonCount] = {};
  float sqr_idx_[kUnisonCount] = {};
  float par_idx_[kUnisonCount] = {};
  float unison_norm_ = 1.f;
  float unison_target_norm_ = 1.f;
  TransportState transport_state_ = TransportState::Idle;
  bool active_ = false;
};
