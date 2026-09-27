/*
 * File: header.c
 *
 * NTS-3 kaoss pad kit generic effect unit header for StepGrain2
 *
 * Order: X / Y / Depth / Edit…
 */

#include "unit_genericfx.h"
#include "dev_id.h"

const __unit_header genericfx_unit_header_t unit_header = {
    .common = {
        .header_size = sizeof(genericfx_unit_header_t),
        .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
        .api = UNIT_API_VERSION,
        .dev_id = MLSA_DEV_ID,
        .unit_id = 0x00000057U,
        .version = MLSA_VERSION_EXPERIMENTAL,
        .name = "StepGrain2",
        .num_params = 7,

        .params = {
            // Format: min, max, center (unused), default, type, frac. bits, frac. mode, <reserved>, name
            {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Fade"}},
            {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Probability"}},
            {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Mix"}},

            {0, 4, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Length"}},
            {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Shimmer"}},
            {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"Spread"}},
            {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"Reverse"}},
            {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        },
    },
    .default_mappings = {
        {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 50},
        {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 100},
        {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 100},

        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 4, 0},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 100},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 100},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 50},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0},
    },
};
