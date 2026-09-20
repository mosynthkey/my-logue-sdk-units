/*
 * File: unit.cc
 *
 * NTS-3 generic effect unit interface for GrainPad
 */

#include "grainpad.h"
#include "unit_genericfx.h"
#include "utils/int_math.h"
#include <algorithm>

static GrainPad s_grainpad_instance;
static unit_runtime_desc_t s_runtime_desc;
static unit_runtime_genericfx_get_raw_input_ptr s_get_raw_input = nullptr;

static int32_t cached_values[UNIT_GENERICFX_MAX_PARAM_COUNT];

__unit_callback int8_t unit_init(const unit_runtime_desc_t *desc)
{
  if (!desc)
    return k_unit_err_undef;

  if (desc->target != unit_header.common.target)
    return k_unit_err_target;

  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;

  if (desc->samplerate != s_grainpad_instance.getSampleRate())
    return k_unit_err_samplerate;

  if (desc->input_channels != 2 || desc->output_channels != 2)
    return k_unit_err_geometry;

  if (!desc->hooks.sdram_alloc)
    return k_unit_err_memory;

  float *allocated_buffer =
      (float *)desc->hooks.sdram_alloc(s_grainpad_instance.getBufferSize() * sizeof(float));
  if (!allocated_buffer)
    return k_unit_err_memory;

  std::fill(allocated_buffer, allocated_buffer + s_grainpad_instance.getBufferSize(), 0.f);
  s_runtime_desc = *desc;
  s_get_raw_input = nullptr;
  if (s_runtime_desc.hooks.runtime_context != nullptr)
  {
    const unit_runtime_genericfx_context_t *fx_context =
        static_cast<const unit_runtime_genericfx_context_t *>(s_runtime_desc.hooks.runtime_context);
    s_get_raw_input = fx_context->get_raw_input;
  }
  s_grainpad_instance.init(allocated_buffer);

  for (uint8_t paramIndex = 0; paramIndex < UNIT_GENERICFX_MAX_PARAM_COUNT; ++paramIndex)
    cached_values[paramIndex] = static_cast<int32_t>(unit_header.common.params[paramIndex].init);

  for (uint8_t paramIndex = 0; paramIndex < unit_header.common.num_params; ++paramIndex)
    s_grainpad_instance.setParameter(paramIndex, cached_values[paramIndex]);

  return k_unit_err_none;
}

__unit_callback void unit_teardown()
{
  s_grainpad_instance.teardown();
  s_get_raw_input = nullptr;
}

__unit_callback void unit_reset()
{
  s_grainpad_instance.reset();
}

__unit_callback void unit_resume()
{
  s_grainpad_instance.resume();
}

__unit_callback void unit_suspend()
{
  s_grainpad_instance.suspend();
}

__unit_callback void unit_render(const float *in, float *out, uint32_t frames)
{
  const float *raw = nullptr;
  if (s_runtime_desc.hooks.runtime_context != nullptr)
  {
    const unit_runtime_genericfx_context_t *fx_context =
        static_cast<const unit_runtime_genericfx_context_t *>(s_runtime_desc.hooks.runtime_context);
    if (fx_context->get_raw_input != nullptr)
      raw = fx_context->get_raw_input();
  }
  if (raw == nullptr && s_get_raw_input != nullptr)
    raw = s_get_raw_input();
  s_grainpad_instance.process(in, raw, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  cached_values[id] = value;
  s_grainpad_instance.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id)
{
  return cached_values[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  return s_grainpad_instance.getParameterStrValue(id, value);
}

__unit_callback void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
{
  s_grainpad_instance.touchEvent(id, phase, x, y);
}

__unit_callback void unit_set_tempo(uint32_t tempo)
{
  float bpm = (tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000);
  s_grainpad_instance.setTempo(bpm);
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter)
{
  s_grainpad_instance.tempo4ppqnTick(counter);
}
