/*
 * File: header.c
 *
 * NTS-1 mkII oscillator unit header for AirHorn
 */

#include "unit_osc.h"
#include "dev_id.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = MLSA_DEV_ID,
    .unit_id = 0x00000008U,
    .version = MLSA_VERSION_PREVIEW,
    .name = "AirHorn",
    .num_params = 2,
    .params = {
        {0, 127, 0, 127, k_unit_param_type_none, 0, 0, 0, {"LEVEL"}},
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"PMODE"}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
};
