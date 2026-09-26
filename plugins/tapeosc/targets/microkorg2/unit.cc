/*
 * File: unit.cc
 *
 * microKORG2 oscillator unit interface for TapeOsc
 *
 */

#include "unit.h"

#include <cstdint>

#include "tapeosc_mk2.h"

static TapeOscMk2 s_tapeosc_instance;
static unit_runtime_desc_t s_runtime_desc;

__attribute__((used)) int8_t unit_init(const unit_runtime_desc_t *desc)
{
  if (!desc)
    return k_unit_err_undef;

  if (desc->target != unit_header.target)
    return k_unit_err_target;
  if (!UNIT_API_IS_COMPAT(desc->api))
    return k_unit_err_api_version;

  s_runtime_desc = *desc;
  return s_tapeosc_instance.Init(desc);
}

__attribute__((used)) void unit_teardown()
{
  s_tapeosc_instance.Teardown();
}

__attribute__((used)) void unit_reset()
{
  s_tapeosc_instance.Reset();
}

__attribute__((used)) void unit_resume()
{
  s_tapeosc_instance.Resume();
}

__attribute__((used)) void unit_suspend()
{
  s_tapeosc_instance.Suspend();
}

__attribute__((used)) void unit_render(const float *in, float *out, uint32_t frames)
{
  (void)in;
  s_tapeosc_instance.Process(out, frames);
}

__attribute__((used)) void unit_set_param_value(uint8_t id, int32_t value)
{
  s_tapeosc_instance.setParameter(id, value);
}

__attribute__((used)) int32_t unit_get_param_value(uint8_t id)
{
  return s_tapeosc_instance.getParameterValue(id);
}

__attribute__((used)) const char *unit_get_param_str_value(uint8_t id, int32_t value)
{
  return s_tapeosc_instance.getParameterStrValue(id, value);
}

__attribute__((used)) const uint8_t *unit_get_param_bmp_value(uint8_t id, int32_t value)
{
  (void)id;
  (void)value;
  return nullptr;
}

__attribute__((used)) void unit_set_tempo(uint32_t tempo)
{
  (void)tempo;
}

__attribute__((used)) void unit_load_preset(uint8_t idx)
{
  (void)idx;
}

__attribute__((used)) uint8_t unit_get_preset_index()
{
  return 0;
}

__attribute__((used)) void unit_platform_exclusive(uint8_t messageId, void *data, uint32_t dataSize)
{
  (void)messageId;
  (void)data;
  (void)dataSize;
}
