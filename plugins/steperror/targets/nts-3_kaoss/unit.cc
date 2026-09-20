/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    Copyright (c) 2026, StepError contributors
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/


/*
 * File: unit.cc
 *
 * NTS-3 generic effect unit interface for StepError
 *
 */

#include "steperror.h"
#include "unit_genericfx.h"
#include "utils/int_math.h"
#include <algorithm>

static StepError s_steperror_instance;
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

  if (desc->samplerate != s_steperror_instance.getSampleRate())
    return k_unit_err_samplerate;

  if (desc->input_channels != 2 || desc->output_channels != 2)
    return k_unit_err_geometry;

  const uint32_t buffer_floats = s_steperror_instance.getBufferSize();
  float *allocated_buffer = nullptr;
  if (buffer_floats > 0U)
  {
    if (!desc->hooks.sdram_alloc)
      return k_unit_err_memory;
    allocated_buffer =
        (float *)desc->hooks.sdram_alloc(buffer_floats * sizeof(float));
    if (!allocated_buffer)
      return k_unit_err_memory;
    std::fill(allocated_buffer, allocated_buffer + buffer_floats, 0.f);
  }

  s_runtime_desc = *desc;
  s_get_raw_input = nullptr;
  if (s_runtime_desc.hooks.runtime_context != nullptr)
  {
    const unit_runtime_genericfx_context_t *fx_context =
        static_cast<const unit_runtime_genericfx_context_t *>(s_runtime_desc.hooks.runtime_context);
    s_get_raw_input = fx_context->get_raw_input;
  }
  s_steperror_instance.init(allocated_buffer);

  for (uint8_t paramIndex = 0; paramIndex < UNIT_GENERICFX_MAX_PARAM_COUNT; ++paramIndex)
    cached_values[paramIndex] = static_cast<int32_t>(unit_header.common.params[paramIndex].init);

  for (uint8_t paramIndex = 0; paramIndex < unit_header.common.num_params; ++paramIndex)
    s_steperror_instance.setParameter(paramIndex, cached_values[paramIndex]);

  return k_unit_err_none;
}

__unit_callback void unit_teardown()
{
  s_steperror_instance.teardown();
  s_get_raw_input = nullptr;
}

__unit_callback void unit_reset()
{
  s_steperror_instance.reset();
}

__unit_callback void unit_resume()
{
  s_steperror_instance.resume();
}

__unit_callback void unit_suspend()
{
  s_steperror_instance.suspend();
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
  s_steperror_instance.process(in, raw, out, frames);
}

__unit_callback void unit_set_param_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  cached_values[id] = value;
  s_steperror_instance.setParameter(id, value);
}

__unit_callback int32_t unit_get_param_value(uint8_t id)
{
  return cached_values[id];
}

__unit_callback const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  value = clipminmaxi32(unit_header.common.params[id].min, value, unit_header.common.params[id].max);
  return s_steperror_instance.getParameterStrValue(id, value);
}

__unit_callback void unit_touch_event(uint8_t id, uint8_t phase, uint32_t x, uint32_t y)
{
  s_steperror_instance.touchEvent(id, phase, x, y);
}

__unit_callback void unit_set_tempo(uint32_t tempo)
{
  float bpm = (tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000);
  s_steperror_instance.setTempo(bpm);
}

__unit_callback void unit_tempo_4ppqn_tick(uint32_t counter)
{
  s_steperror_instance.tempo4ppqnTick(counter);
}
