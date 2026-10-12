/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once

#include "Sampler.hpp"
#include "RawInputFormat.hpp"
#include "IQPipeline.hpp"
#include "LowPassFilter.hpp"

enum class IQPipelineOptions {
    NONE,
    IQ_FIR_INTERNAL,
    IQ_FIR_FILE
};


template<typename RawFormat, typename Sampler, IQPipelineOptions Opt>
struct Preset {
    using RawFormatType = RawFormat;
    using SamplerType   = Sampler;
    using RawType       = typename RawFormat::RawType;

    static constexpr SampleRate        inputRate      = SamplerType::InputSampleRate;
    static constexpr SampleRate        outputRate     = SamplerType::OutputSampleRate;
    static constexpr IQPipelineOptions pipelineOption = Opt;
};



#if defined(STREAM1090_CUSTOM_INPUT) && STREAM1090_CUSTOM_INPUT
#define PRESET_LOADER_FILENAME "../presets/CustomPresets.hpp"
#elif defined(STREAM1090_TOO_MUCH_CPU) && STREAM1090_TOO_MUCH_CPU
#define PRESET_LOADER_FILENAME "../presets/AirspyPresets.hpp"
#else
#define PRESET_LOADER_FILENAME "../presets/DefaultPresets.hpp"
#endif

#include "PresetsLoader.hpp"
constexpr auto presets = loaded_presets;
#undef PRESET_LOADER_FILENAME


template<SampleRate In, SampleRate Out, typename RawFormat, IQPipelineOptions sel>
struct IQPipelineSelector {
    static auto make(const std::vector<float>&) {
        return make_pipeline();
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQ_UINT16_RAW_AIRSPY, IQPipelineOptions::NONE> {
    static auto make(const std::vector<float>&) {
        return make_pipeline(DCRemoval(), FlipSigns());
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQ_UINT16_RAW_AIRSPY, IQPipelineOptions::IQ_FIR_INTERNAL> {
    static auto make(const std::vector<float>&) {
        return make_pipeline(DCRemoval(), FlipSigns(), IQLowPass<In, Out>());
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQ_UINT16_RAW_AIRSPY, IQPipelineOptions::IQ_FIR_FILE> {
    static auto make(const std::vector<float>& taps) {
        return make_pipeline(DCRemoval(), FlipSigns(), IQLowPassDynamic(taps));
    }
};

template<SampleRate In, SampleRate Out, typename RawFormat>
struct IQPipelineSelector<In, Out, RawFormat, IQPipelineOptions::IQ_FIR_INTERNAL> {
    static auto make(const std::vector<float>&) {
        return make_pipeline(IQLowPass<In, Out>());
    }
};

template<SampleRate In, SampleRate Out, typename RawFormat>
struct IQPipelineSelector<In, Out, RawFormat, IQPipelineOptions::IQ_FIR_FILE> {
    static auto make(const std::vector<float>& taps) {
        return make_pipeline(IQLowPassDynamic(taps));
    }
};

