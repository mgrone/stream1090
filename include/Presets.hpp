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
    IQ_FIR,
    IQ_FIR_FILE,
    IQ_FIR_RTL_SDR,
    IQ_FIR_RTL_SDR_FILE
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
constexpr auto presets = std::make_tuple(
    // Custom Input
    Preset<IQ_FLOAT32, Sampler_2_0_to_2_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_FLOAT32, Sampler_2_0_to_4_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_FLOAT32, Sampler_2_0_to_8_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_FLOAT32, Sampler_4_0_to_4_0_Mhz, IQPipelineOptions::NONE>{}
);
#else 
constexpr auto presets = std::make_tuple(
    // RTL-SDR (uint8)
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_4_to_8_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_4_to_8_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_4_to_8_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR_FILE>{},

    Preset<IQ_UINT8_RTL_SDR, Sampler_2_4_to_12_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_4_to_12_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_4_to_12_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR_FILE>{},

    Preset<IQ_UINT8_RTL_SDR, Sampler_2_56_to_8_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_56_to_8_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_56_to_8_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR_FILE>{} ,
    
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_56_to_12_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_56_to_12_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR>{},
    Preset<IQ_UINT8_RTL_SDR, Sampler_2_56_to_12_0_Mhz, IQPipelineOptions::IQ_FIR_RTL_SDR_FILE>{} ,
    

    // Airspy (uint16)
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_6_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_12_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_24_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_10_0_to_10_0_Mhz, IQPipelineOptions::NONE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_10_0_to_24_0_Mhz, IQPipelineOptions::NONE>{},

    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_6_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_12_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_24_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_10_0_to_10_0_Mhz, IQPipelineOptions::IQ_FIR>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_10_0_to_24_0_Mhz, IQPipelineOptions::IQ_FIR>{},

    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_6_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_12_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_6_0_to_24_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_10_0_to_10_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{},
    Preset<IQ_UINT16_RAW_AIRSPY, Sampler_10_0_to_24_0_Mhz, IQPipelineOptions::IQ_FIR_FILE>{}
);

#endif


template<SampleRate In, SampleRate Out, IQPipelineOptions sel>
struct IQPipelineSelector {
    static auto make(const std::vector<float>&) {
        return make_pipeline();
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQPipelineOptions::IQ_FIR> {
    static auto make(const std::vector<float>&) {
        return make_pipeline(DCRemoval(), FlipSigns(), IQLowPass<In, Out>());
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQPipelineOptions::IQ_FIR_FILE> {
    static auto make(const std::vector<float>& taps) {
        return make_pipeline(DCRemoval(), FlipSigns(), IQLowPassDynamic(taps));
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQPipelineOptions::IQ_FIR_RTL_SDR> {
    static auto make(const std::vector<float>&) {
        return make_pipeline(IQLowPass<In, Out>());
    }
};

template<SampleRate In, SampleRate Out>
struct IQPipelineSelector<In, Out, IQPipelineOptions::IQ_FIR_RTL_SDR_FILE> {
    static auto make(const std::vector<float>& taps) {
        return make_pipeline(IQLowPassDynamic(taps));
    }
};

