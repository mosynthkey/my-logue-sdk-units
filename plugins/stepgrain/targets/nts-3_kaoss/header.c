/*
 * File: header.c
 *
 * NTS-3 kaoss pad kit generic effect unit header for StepGrain
 * (string params follow dummy-genericfx layout)
 *
 * Order: X / Y / Depth / Edit… — MODE sits next to MIX; STEPS before ENV.
 */

#include "unit_genericfx.h"
#include "dev_id.h"

// ---- Unit header definition  --------------------------------------------------------------------

const __unit_header genericfx_unit_header_t unit_header = {
    .common = {
        .header_size = sizeof(genericfx_unit_header_t),
        .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
        .api = UNIT_API_VERSION,
        .dev_id = MLSA_DEV_ID,
        .unit_id = 0x0000000CU,
        .version = MLSA_VERSION_EXPERIMENTAL,
        .name = "StepGrain",
        .num_params = 8, // Number of valid parameter descriptors. (max. 8)

        .params = {
            // Format: min, max, center (unused), default, type, frac. bits, frac. mode, <reserved>, name
            // See common/runtime.h for type enum and unit_param_t structure

            {0, 1023, 0, 1023, k_unit_param_type_none, 0, 0, 0, {"FEEL"}},
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"OCT"}},
            {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"MIX"}},

            // Strings type parameters (same pattern as dummy-genericfx PARAM4)
            {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"MODE"}},
            {0, 7, 0, 6, k_unit_param_type_strings, 0, 0, 0, {"STEPS"}},
            {0, 6, 0, 2, k_unit_param_type_strings, 0, 0, 0, {"ENV"}},

            {0, 100, 0, 100, k_unit_param_type_percent, 0, 0, 0, {"SPRD"}},
            {0, 100, 0, 50, k_unit_param_type_percent, 0, 0, 0, {"REVS"}},
        },
    },
    .default_mappings = {
        // Format: assign, curve, curve polarity, min, max, default value

        {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 1023},
        {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        // MIX = Depth (live vs grains)
        {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 100},

        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1, 0},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 7, 6},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 6, 2},

        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 100},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 100, 50},
    },
};
