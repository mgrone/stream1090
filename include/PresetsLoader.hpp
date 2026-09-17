#pragma once

#define Airspy IQ_UINT16_RAW_AIRSPY
#define RtlSdr IQ_UINT8_RTL_SDR
#define Signed_12bit IQ_INT16_ANTSDR

#define None IQPipelineOptions::NONE
#define Internal IQPipelineOptions::IQ_FIR_RTL_SDR
#define File IQPipelineOptions::IQ_FIR_RTL_SDR_FILE

#define AirspyInternal IQPipelineOptions::IQ_FIR
#define AirspyFile IQPipelineOptions::IQ_FIR_FILE

template<
    typename RawFormat,
    SampleRate InputRate,
    SampleRate OutputRate,
    IQPipelineOptions Opt
>
constexpr auto make_preset() {
    static_assert(
        InputRate <= OutputRate,
        "Input rate larger than output rate");

    static_assert(
        OutputRate % Rate_2_0_Mhz == 0,
        "Output rate is not a multiple of 2 MHz");

    using Sampler = SamplerBase<InputRate, OutputRate>;

    return Preset<
        RawFormat,
        Sampler,
        Opt
    >{};
}

#define ADD_PRESET(IN_TYPE, IN_RATE, OUT_RATE, FILTER, ...) \
    std::make_tuple(                                        \
    make_preset<                                            \
        IN_TYPE,                                            \
        static_cast<SampleRate>((IN_RATE) * 1000000),       \
        static_cast<SampleRate>((OUT_RATE) * 1000000),      \
        FILTER                                              \
    >()),

constexpr auto custom_presets = std::tuple_cat(
#include "../presets/CustomPresets.hpp"
std::make_tuple()
);

#undef ADD_PRESET

#undef Airspy
#undef RtlSdr
#undef Signed_12bit

#undef None
#undef Internal
#undef File

#undef AirspyInternal
#undef AirspyFile

