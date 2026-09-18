/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    Copyright (c) 2026, Ride909 contributors
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
 * File: header.c
 *
 * NTS-3 generic effect unit header for Ride909
 * Voice: TR-909 Ride 6-bit ROM + analog reconstruction.
 *
 */

#include "unit_genericfx.h"
#include "dev_id.h"

const __unit_header genericfx_unit_header_t unit_header = {
    .common = {
        .header_size = sizeof(genericfx_unit_header_t),
        .target = UNIT_TARGET_PLATFORM | k_unit_module_genericfx,
        .api = UNIT_API_VERSION,
        .dev_id = MLSA_DEV_ID,
        .unit_id = 0x00000005U,
        .version = MLSA_VERSION_PREVIEW,
        .name = "Ride909",
        .num_params = 6,
        .params = {
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"PITCH"}},
            {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"PUMP"}},
            {0, 1000, 0, 1000, k_unit_param_type_percent, 1, 1, 0, {"MIX"}},
            {0, 1023, 0, 512, k_unit_param_type_none, 0, 0, 0, {"TONE"}},
            {0, 1023, 0, 1023, k_unit_param_type_none, 0, 0, 0, {"DEC"}},
            {0, 1023, 0, 0, k_unit_param_type_none, 0, 0, 0, {"GAIN"}},
            {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
            {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}},
    },
    .default_mappings = {
        // Unipolar: Edit knob and X pad share the same linear 0..1023 path
        // (bipolar added a center dead-zone and remapped the sides).
        {k_genericfx_param_assign_x, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        {k_genericfx_param_assign_y, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 0},
        {k_genericfx_param_assign_depth, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1000, 1000},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 512},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 1023},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 1023, 0},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0},
        {k_genericfx_param_assign_none, k_genericfx_curve_linear, k_genericfx_curve_unipolar, 0, 0, 0},
    },
};
