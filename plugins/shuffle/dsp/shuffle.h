#pragma once

/*
 * File: shuffle.h
 *
 * Glitch²-style Shuffle: freeze ~1 bar and jump between random sub-slices.
 * Prefer get_raw_input while the pad is up (unit_render may be muted).
 * Y while held sets slice / rate. Depth = MIX. Untouched = bypass.
 */

#include "macros.h"
#include "processor.h"
#include "runtime.h"
#include "utils/float_math.h"
#include <stdint.h>

class Shuffle : public Processor
{
public:
  static constexpr uint32_t kMaxBufSamples = 288000U;
  static constexpr uint32_t kMinSliceSamples = 64U;
  static constexpr uint32_t kMinCaptureSamples = 1024U;
  static constexpr uint32_t kXfadeSamples = 96U;
  static constexpr float kMinBpm = 40.f;
  static constexpr float kMaxBpm = 300.f;
  static constexpr float kMinCapturePeak = 0.003f;
  static constexpr float kWetFadeSamples = 384.f;

  uint32_t getBufferSize() const override final
  {
    return kMaxBufSamples * 2U;
  }

  enum
  {
    MIX = 0U,
    DECAY,
    SYNC,
    HOLD,
    NUM_PARAMS
  };

  enum
  {
    SYNC_EVEN = 0,
    SYNC_TRIP,
    SYNC_DOT,
    SYNC_FREE,
    NUM_SYNCS
  };

  enum
  {
    HOLD_GATE = 0,
    HOLD_LATCH,
    NUM_HOLDS
  };

  void setParameter(uint8_t index, int32_t value) override final
  {
    switch (index)
    {
    case MIX:
      mix_ = value / 1000.f;
      if (mix_ < 0.f)
        mix_ = 0.f;
      if (mix_ > 1.f)
        mix_ = 1.f;
      break;
    case DECAY:
      decay_norm_ = param_10bit_to_f32(value);
      break;
    case SYNC:
    {
      int32_t sync = value;
      if (sync < 0)
        sync = 0;
      if (sync >= NUM_SYNCS)
        sync = NUM_SYNCS - 1;
      if (sync_ != static_cast<uint8_t>(sync))
      {
        sync_ = static_cast<uint8_t>(sync);
        updateSliceLength();
      }
      break;
    }
    case HOLD:
    {
      int32_t hold = value;
      if (hold < 0)
        hold = 0;
      if (hold >= NUM_HOLDS)
        hold = NUM_HOLDS - 1;
      hold_ = static_cast<uint8_t>(hold);
      if (hold_ == HOLD_GATE && !pad_held_ && active_)
        requestRelease();
      break;
    }
    default:
      break;
    }
  }

  const char *getParameterStrValue(uint8_t index, int32_t value) const override final
  {
    static const char *sync_names[NUM_SYNCS] = {"EVEN", "TRIP", "DOT", "FREE"};
    static const char *hold_names[NUM_HOLDS] = {"GATE", "LATCH"};

    if (index == SYNC && value >= 0 && value < NUM_SYNCS)
      return sync_names[value];
    if (index == HOLD && value >= 0 && value < NUM_HOLDS)
      return hold_names[value];
    return nullptr;
  }

  void init(float *allocated_buffer) override final
  {
    buf_left_ = allocated_buffer;
    buf_right_ = allocated_buffer + kMaxBufSamples;

    for (uint32_t sampleIndex = 0; sampleIndex < getBufferSize(); ++sampleIndex)
      allocated_buffer[sampleIndex] = 0.f;

    time_norm_ = 0.55f;
    mix_ = 1.f;
    decay_norm_ = 0.25f;
    sync_ = SYNC_EVEN;
    hold_ = HOLD_GATE;
    bpm_ = 120.f;
    updateLoopGeometry();
    updateSliceLength();
    reset();
  }

  void teardown() override final
  {
    buf_left_ = nullptr;
    buf_right_ = nullptr;
  }

  void reset() override final
  {
    write_pos_ = 0U;
    captured_samples_ = 0U;
    arm_samples_ = 0U;
    captured_peak_ = 0.f;
    pad_held_ = false;
    active_ = false;
    arming_ = false;
    wet_ = 0.f;
    wet_target_ = 0.f;
    touch_y_ = 512.f;
    play_pos_ = 0.f;
    play_dir_ = 1.f;
    repeat_gain_ = 1.f;
    gate_phase_ = 0.f;
    gate_level_ = 0.f;
    rng_state_ = 0xA5A5A5A5U;
    frozen_origin_ = 0U;
    frozen_length_ = 0U;
    slice_play_length_ = kMinSliceSamples;
    shuf_origin_ = 0U;
    shuf_reverse_ = false;

    if (buf_left_ != nullptr)
    {
      for (uint32_t sampleIndex = 0; sampleIndex < kMaxBufSamples; ++sampleIndex)
      {
        buf_left_[sampleIndex] = 0.f;
        buf_right_[sampleIndex] = 0.f;
      }
    }
  }

  void setTempo(float tempo) override final
  {
    if (tempo >= kMinBpm && tempo <= kMaxBpm)
    {
      bpm_ = tempo;
      updateLoopGeometry();
      updateSliceLength();
    }
  }

  void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) override final
  {
    (void)id;
    (void)x;

    if (phase == k_unit_touch_phase_ended || phase == k_unit_touch_phase_cancelled)
    {
      pad_held_ = false;
      if (hold_ == HOLD_GATE)
        requestRelease();
      return;
    }

    if (phase != k_unit_touch_phase_began && phase != k_unit_touch_phase_moved &&
        phase != k_unit_touch_phase_stationary)
      return;

    touch_y_ = static_cast<float>(y);

    const bool new_touch = !pad_held_;
    pad_held_ = true;

    if (phase == k_unit_touch_phase_began || new_touch)
      engageEffect();

    updateTimeFromTouch();
  }

  void process(const float *__restrict in, float *__restrict out, uint32_t frames) override final
  {
    process(in, nullptr, out, frames);
  }

  void process(const float *__restrict in, const float *__restrict raw, float *__restrict out, uint32_t frames)
  {
    for (uint32_t sampleIndex = 0; sampleIndex < frames; ++sampleIndex)
    {
      float live_left = in[0];
      float live_right = in[1];
      float rec_left = live_left;
      float rec_right = live_right;
      if (raw != nullptr)
      {
        const float in_energy = absf(live_left) + absf(live_right);
        const float raw_energy = absf(raw[0]) + absf(raw[1]);
        if (raw_energy > in_energy)
        {
          rec_left = raw[0];
          rec_right = raw[1];
          if (pad_held_ || active_)
          {
            live_left = rec_left;
            live_right = rec_right;
          }
        }
      }

      const bool freeze_playback = needsFrozenAudio() && !arming_ && (active_ || wet_ > 0.f);
      if (!freeze_playback)
        recordSample(rec_left, rec_right);

      if (arming_)
      {
        if (arm_samples_ < 0xFFFFFFF0U)
          ++arm_samples_;
        if (arm_samples_ >= neededCaptureSamples() && captured_peak_ >= kMinCapturePeak)
        {
          arming_ = false;
          freezeWindow(neededCaptureSamples());
          startVoice();
          wet_target_ = 1.f;
        }
      }

      advanceWet();

      if (wet_ <= 0.f)
      {
        out[0] = live_left;
        out[1] = live_right;
        in += 2;
        if (raw != nullptr)
          raw += 2;
        out += 2;
        continue;
      }

      float fx_left = live_left;
      float fx_right = live_right;
      renderEffect(live_left, live_right, fx_left, fx_right);

      const float wet_gain = wet_ * mix_;
      const float dry_gain = 1.f - wet_gain;
      out[0] = live_left * dry_gain + fx_left * wet_gain;
      out[1] = live_right * dry_gain + fx_right * wet_gain;

      in += 2;
      if (raw != nullptr)
        raw += 2;
      out += 2;
    }
  }

  bool isActive() const { return active_; }
  bool isArming() const { return arming_; }
  bool isPadHeld() const { return pad_held_; }
  float wetAmount() const { return wet_; }
  float capturedPeak() const { return captured_peak_; }
  uint32_t capturedSamples() const { return captured_samples_; }
  uint32_t sliceLength() const { return slice_length_; }

private:
  static float absf(float value)
  {
    return value < 0.f ? -value : value;
  }

  static uint32_t wrapIndex(uint32_t index, uint32_t length)
  {
    if (length == 0U)
      return 0U;
    while (index >= length)
      index -= length;
    return index;
  }

  static constexpr bool needsFrozenAudio()
  {
    return true;
  }

  uint32_t neededCaptureSamples() const
  {
    if (true)
      return loop_length_ < kMinCaptureSamples ? kMinCaptureSamples : loop_length_;
    return slice_length_ < kMinCaptureSamples ? kMinCaptureSamples : slice_length_;
  }

  bool hasUsableCapture(uint32_t needed) const
  {
    return captured_samples_ >= needed && captured_peak_ >= kMinCapturePeak;
  }

  void requestRelease()
  {
    wet_target_ = 0.f;
    arming_ = false;
  }

  void engageEffect()
  {
    updateTimeFromTouch();
    active_ = true;
    arming_ = false;
    rng_state_ ^= write_pos_ + 0x9E3779B9U;

    if (!needsFrozenAudio())
    {
      startVoice();
      wet_target_ = 1.f;
      return;
    }

    const uint32_t needed = neededCaptureSamples();
    if (hasUsableCapture(needed))
    {
      freezeWindow(needed);
      startVoice();
      wet_target_ = 1.f;
      return;
    }

    arming_ = true;
    arm_samples_ = 0U;
    captured_peak_ = 0.f;
    wet_target_ = 0.f;
  }

  void startVoice()
  {
    play_pos_ = 0.f;
    play_dir_ = 1.f;
    repeat_gain_ = 1.f;
    gate_phase_ = 0.f;
    shuf_origin_ = 0U;
    shuf_reverse_ = false;
    slice_play_length_ = slice_length_;
    if (slice_play_length_ > frozen_length_ && frozen_length_ >= kMinSliceSamples)
      slice_play_length_ = frozen_length_;
    if (slice_play_length_ < kMinSliceSamples)
      slice_play_length_ = kMinSliceSamples;

    if (true)
      pickShuffleSlice();
    if (false)
    {
      play_pos_ = static_cast<float>(slice_play_length_ > 1U ? slice_play_length_ - 1U : 0U);
      play_dir_ = -1.f;
    }
  }

  void freezeWindow(uint32_t length)
  {
    if (length > captured_samples_)
      length = captured_samples_;
    if (length < kMinSliceSamples)
      length = kMinSliceSamples;
    if (length > record_length_)
      length = record_length_;

    frozen_length_ = length;
    const uint32_t newest_index = write_pos_ == 0U ? record_length_ - 1U : write_pos_ - 1U;
    int32_t origin = static_cast<int32_t>(newest_index) - static_cast<int32_t>(length) + 1;
    if (origin < 0)
      origin += static_cast<int32_t>(record_length_);
    frozen_origin_ = static_cast<uint32_t>(origin);
  }

  void updateLoopGeometry()
  {
    const float seconds_per_bar = 240.f / bpm_;
    uint32_t samples = static_cast<uint32_t>(seconds_per_bar * getSampleRate() + 0.5f);
    if (samples < kMinCaptureSamples)
      samples = kMinCaptureSamples;
    if (samples > kMaxBufSamples)
      samples = kMaxBufSamples;
    loop_length_ = samples;
    record_length_ = samples;
  }

  float timeBeats() const
  {
    const float min_beats = 0.0625f;
    const float max_beats = 2.f;
    float beats;
    if (sync_ == SYNC_FREE)
    {
      beats = max_beats * fasterpowf(min_beats / max_beats, time_norm_);
    }
    else
    {
      static const float kEvenBeats[6] = {2.f, 1.f, 0.5f, 0.25f, 0.125f, 0.0625f};
      const float select = time_norm_ * 5.0001f;
      uint32_t step = static_cast<uint32_t>(select);
      if (step > 5U)
        step = 5U;
      beats = kEvenBeats[step];
      if (sync_ == SYNC_TRIP)
        beats *= 2.f / 3.f;
      else if (sync_ == SYNC_DOT)
        beats *= 1.5f;
    }
    return beats;
  }

  void updateTimeFromTouch()
  {
    time_norm_ = touch_y_ * (1.f / 1023.f);
    if (time_norm_ < 0.f)
      time_norm_ = 0.f;
    if (time_norm_ > 1.f)
      time_norm_ = 1.f;
    updateSliceLength();
  }

  void updateSliceLength()
  {
    float samples = timeBeats() * 60.f / bpm_ * getSampleRate();
    if (samples < static_cast<float>(kMinSliceSamples))
      samples = static_cast<float>(kMinSliceSamples);
    if (samples > static_cast<float>(loop_length_))
      samples = static_cast<float>(loop_length_);
    slice_length_ = static_cast<uint32_t>(samples + 0.5f);
    if (slice_length_ < kMinSliceSamples)
      slice_length_ = kMinSliceSamples;
    if (active_ && !arming_)
    {
      slice_play_length_ = slice_length_;
      if (slice_play_length_ > frozen_length_ && frozen_length_ >= kMinSliceSamples)
        slice_play_length_ = frozen_length_;
    }
  }

  void recordSample(float left, float right)
  {
    if (buf_left_ == nullptr || record_length_ == 0U)
      return;

    buf_left_[write_pos_] = left;
    buf_right_[write_pos_] = right;
    ++write_pos_;
    if (write_pos_ >= record_length_)
      write_pos_ = 0U;
    if (captured_samples_ < record_length_)
      ++captured_samples_;

    const float peak = absf(left) > absf(right) ? absf(left) : absf(right);
    if (peak > captured_peak_)
      captured_peak_ = peak;
  }

  void advanceWet()
  {
    const float increment = 1.f / kWetFadeSamples;
    if (wet_ < wet_target_)
    {
      wet_ += increment;
      if (wet_ > wet_target_)
        wet_ = wet_target_;
    }
    else if (wet_ > wet_target_)
    {
      wet_ -= increment;
      if (wet_ < wet_target_)
        wet_ = wet_target_;
    }

    if (wet_ <= 0.f && wet_target_ <= 0.f && !arming_)
      active_ = false;
  }

  float nextRandom()
  {
    rng_state_ = rng_state_ * 1664525U + 1013904223U;
    return static_cast<float>((rng_state_ >> 8) & 0x00FFFFFFU) * (1.f / 16777216.f);
  }

  void readFrozen(float pos, uint32_t window, float &left, float &right) const
  {
    if (buf_left_ == nullptr || window == 0U)
    {
      left = 0.f;
      right = 0.f;
      return;
    }

    float wrapped = pos;
    const float window_f = static_cast<float>(window);
    while (wrapped >= window_f)
      wrapped -= window_f;
    while (wrapped < 0.f)
      wrapped += window_f;

    const uint32_t index_a = static_cast<uint32_t>(wrapped);
    const float frac = wrapped - static_cast<float>(index_a);
    const uint32_t index_b = wrapIndex(index_a + 1U, window);
    const uint32_t abs_a = wrapIndex(frozen_origin_ + index_a, record_length_);
    const uint32_t abs_b = wrapIndex(frozen_origin_ + index_b, record_length_);
    left = buf_left_[abs_a] + (buf_left_[abs_b] - buf_left_[abs_a]) * frac;
    right = buf_right_[abs_a] + (buf_right_[abs_b] - buf_right_[abs_a]) * frac;
  }

  void readFrozenWithXfade(float pos, uint32_t window, float &left, float &right) const
  {
    readFrozen(pos, window, left, right);
    if (window <= kXfadeSamples + 8U)
      return;

    const float xfade = static_cast<float>(kXfadeSamples);
    const float window_f = static_cast<float>(window);
    if (pos + xfade >= window_f)
    {
      const float fade = (pos + xfade - window_f) / xfade;
      float start_left = 0.f;
      float start_right = 0.f;
      readFrozen(pos + xfade - window_f, window, start_left, start_right);
      left = left * (1.f - fade) + start_left * fade;
      right = right * (1.f - fade) + start_right * fade;
    }
  }

  void pickShuffleSlice()
  {
    uint32_t window = frozen_length_ < slice_length_ ? frozen_length_ : slice_length_;
    if (window < kMinSliceSamples)
      window = kMinSliceSamples;
    slice_play_length_ = window;

    const uint32_t range = frozen_length_ > window ? frozen_length_ - window : 0U;
    shuf_origin_ = range == 0U ? 0U : static_cast<uint32_t>(nextRandom() * static_cast<float>(range));
    shuf_reverse_ = nextRandom() < 0.28f;
    play_pos_ = shuf_reverse_ ? static_cast<float>(window - 1U) : 0.f;
    play_dir_ = shuf_reverse_ ? -1.f : 1.f;
    repeat_gain_ = 1.f;
  }

  void renderRetrigger(float &left, float &right)
  {
    const uint32_t window = slice_play_length_ < kMinSliceSamples ? kMinSliceSamples : slice_play_length_;
    readFrozenWithXfade(play_pos_, window, left, right);
    left *= repeat_gain_;
    right *= repeat_gain_;
    play_pos_ += 1.f;
    if (play_pos_ >= static_cast<float>(window))
    {
      play_pos_ -= static_cast<float>(window);
      repeat_gain_ *= 1.f - decay_norm_ * 0.38f;
      if (repeat_gain_ < 0.02f)
        repeat_gain_ = 0.f;
    }
  }

  void renderReverse(float &left, float &right)
  {
    const uint32_t window = slice_play_length_ < kMinSliceSamples ? kMinSliceSamples : slice_play_length_;
    readFrozen(play_pos_, window, left, right);
    play_pos_ += play_dir_;
    if (play_pos_ >= static_cast<float>(window))
    {
      play_pos_ = static_cast<float>(window) - 1.f;
      play_dir_ = -1.f;
    }
    else if (play_pos_ < 0.f)
    {
      play_pos_ = 0.f;
      play_dir_ = 1.f;
    }
  }

  void renderShuffle(float &left, float &right)
  {
    const uint32_t window = slice_play_length_ < kMinSliceSamples ? kMinSliceSamples : slice_play_length_;
    float pos = play_pos_ + static_cast<float>(shuf_origin_);
    readFrozen(pos, frozen_length_ < window ? window : frozen_length_, left, right);
    left *= repeat_gain_;
    right *= repeat_gain_;
    play_pos_ += play_dir_;
    const bool ended = shuf_reverse_ ? play_pos_ < 0.f : play_pos_ >= static_cast<float>(window);
    if (ended)
    {
      if (nextRandom() < 0.22f)
      {
        play_pos_ = shuf_reverse_ ? static_cast<float>(window - 1U) : 0.f;
        repeat_gain_ *= 1.f - decay_norm_ * 0.25f;
      }
      else
      {
        pickShuffleSlice();
      }
    }
  }

  void renderGate(float live_left, float live_right, float &left, float &right)
  {
    const float step = static_cast<float>(slice_length_ < kMinSliceSamples ? kMinSliceSamples : slice_length_);
    gate_phase_ += 1.f;
    if (gate_phase_ >= step)
      gate_phase_ -= step;
    const float pulse = gate_phase_ < step * 0.5f ? 1.f : 0.f;
    const float smooth = 0.04f + decay_norm_ * 0.35f;
    gate_level_ += (pulse - gate_level_) * (1.f - smooth);
    left = live_left * gate_level_;
    right = live_right * gate_level_;
  }

  void renderEffect(float live_left, float live_right, float &left, float &right)
  {
    renderShuffle(left, right);
    (void)live_left;
    (void)live_right;
  }

  float *buf_left_ = nullptr;
  float *buf_right_ = nullptr;

  float time_norm_ = 0.55f;
  float mix_ = 1.f;
  float decay_norm_ = 0.25f;
  float bpm_ = 120.f;
  float wet_ = 0.f;
  float wet_target_ = 0.f;
  float touch_y_ = 512.f;
  float play_pos_ = 0.f;
  float play_dir_ = 1.f;
  float repeat_gain_ = 1.f;
  float gate_phase_ = 0.f;
  float gate_level_ = 0.f;
  float captured_peak_ = 0.f;

  uint32_t loop_length_ = 96000U;
  uint32_t record_length_ = 96000U;
  uint32_t slice_length_ = 6000U;
  uint32_t slice_play_length_ = 6000U;
  uint32_t frozen_origin_ = 0U;
  uint32_t frozen_length_ = 0U;
  uint32_t shuf_origin_ = 0U;
  uint32_t write_pos_ = 0U;
  uint32_t captured_samples_ = 0U;
  uint32_t arm_samples_ = 0U;
  uint32_t rng_state_ = 0xA5A5A5A5U;
  uint8_t sync_ = SYNC_EVEN;
  uint8_t hold_ = HOLD_GATE;
  bool pad_held_ = false;
  bool active_ = false;
  bool arming_ = false;
  bool shuf_reverse_ = false;
};
