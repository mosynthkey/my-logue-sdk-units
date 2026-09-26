/**
 *  @file header.c
 *  @brief microKORG2 TapeOsc oscillator unit header
 *
 *  Copyright (c) 2026 TapeOsc contributors
 *
 */

#include "unit.h"
#include "runtime.h"

#include "dev_id.h"
#include "mk2_dev_id.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = MLSA_DEV_ID,
    .unit_id = MK2_UNIT_ID_TAPEOSC,
    .version = MLSA_VERSION_EXPERIMENTAL,
    .name = "TapeOsc",
    .num_presets = 0,
    .num_params = 4,
    .params = {
        {0, 3, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"WAVE"}},
        {10, 2000, 10, 93, k_unit_param_type_msec, 0, 0, 0, {"START"}},
        {10, 4000, 10, 558, k_unit_param_type_msec, 0, 0, 0, {"STOP"}},
        {0, 100, 0, 0, k_unit_param_type_percent, 0, 0, 0, {"WOW"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}}};
