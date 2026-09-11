/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#include "PresetDispatcher.hpp"
#include "Presets.hpp"

#if !defined(STREAM1090_CUSTOM_INPUT) || !STREAM1090_CUSTOM_INPUT

std::optional<bool> runAirspyPresets(const CompileTimeVars& compileTimeVars,
                                     const RuntimeVars& runtimeVars) {
    return runPresetGroup(airspyPresets, compileTimeVars, runtimeVars);
}

#endif