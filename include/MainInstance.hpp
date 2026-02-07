/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once
#include "Global.hpp"
#include "Sampler.hpp"
#include "RingBuffer.hpp"
#include "Presets.hpp"
#include "SampleStream.hpp"
#include "InputStreamReader.hpp"
#include "InputBufferReader.hpp"
#include "IQPipeline.hpp"
#include "LowPassFilter.hpp"
#include "devices/IniConfig.hpp"
#include "devices/DeviceFactory.hpp"


template<typename Sampler>
void printSamplerConfig() {
    std::cerr << "[Stream1090] build " << STREAM1090_VERSION << std::endl;
    std::cerr << "[Stream1090] Input sampling speed: " << (double)Sampler::InputSampleRate / 1000000.0 << " MHz" << std::endl;
    std::cerr << "[Stream1090] Output sampling speed: " << Sampler::OutputSampleRate / 1000000 << " MHz" << std::endl;
    std::cerr << "[Stream1090] Input to output ratio: " << Sampler::RatioInput << ":" << Sampler::RatioOutput << std::endl;
    std::cerr << "[Stream1090] Number of streams: " << Sampler::NumStreams << std::endl;
    std::cerr << "[Stream1090] Size of input buffer: " << Sampler::InputBufferSize << " samples " << std::endl;
    std::cerr << "[Stream1090] Size of sample buffer: " << Sampler::SampleBufferSize << " samples " << std::endl;  
}

struct CompileTimeVars {
    RawFormat rawFormat = RawFormat::UINT_8_IQ;
    SampleRate inputRate = Rate_2_4_Mhz;
    SampleRate outputRate = Rate_8_0_Mhz;
    IQPipelineOptions pipelineOption = IQPipelineOptions::NONE;
};

struct RuntimeVars {
    InputDeviceType deviceType = InputDeviceType::STREAM;
    IniConfig::Section deviceConfigSection;
    std::vector<float> filterTaps;
    bool verbose = true;
};

 // this class serves to hold all compile and runtime information
template<typename preset>
class MainInstance {
public:
    MainInstance(const RuntimeVars& runtimeVars) : m_runtimeVars(runtimeVars) { 
        printSamplerConfig<SamplerType>();
    }

    // we first unpack the preset
    using RawType = typename preset::RawType;
    static constexpr SampleRate inputRate  = preset::inputRate;
    static constexpr SampleRate outputRate = preset::outputRate;
    static constexpr IQPipelineOptions pipelineOption = preset::pipelineOption;
    typedef SamplerBase<inputRate , outputRate> SamplerType;

    // with all the compile time information available we continue now with what we need
    typedef std::unique_ptr<InputDeviceBase<RawType> > DevicePtr;
    using RingBuffer = RingBufferAsync<RawType, SamplerType::InputBufferSize * 2>;
    using Writer = typename RingBuffer::Writer;
    
    bool setup_device() {
        const auto& cfg = m_runtimeVars.deviceConfigSection;
        
        // before we open, we check the serial
        uint64_t serial = 0;
        if (cfg.contains("serial")) {
            serial = std::stoull(cfg.at("serial"), nullptr, 0);
        }

        // let us try to open the device
        if (!m_device->open_with_serial(serial)) {
            // this is not good at all
            return false;
        };

        // device is ready, apply all the other properties
        for (auto& [key, value] : cfg) {
            if (key == "serial")
                continue;
            m_device->applySetting(key, value);
        }

        // we do not care if any of the properties did not work
        return true;
   }

    void run_async_device(auto& iqPipeline) {
        RingBuffer ringBuffer;
        Writer writer(ringBuffer);

        // setup the device with writer
        m_device = DeviceFactory<RawType>::create(m_runtimeVars.deviceType, inputRate, writer);
        
        if (m_device == nullptr) {
            log("[Stream1090] Device instantiation failed.");
            return;
        }
        log("[Stream1090] Device created.");

        if (!setup_device()) {
            log("[Stream1090] Device configuration failed.");
            return;
        }
        log("[Stream1090] Device successfully configured.");

        if (!m_device->start()) {
            log("[Stream1090] Device refuses to start. Aborting.");
            return;
        };

        log("[Stream1090] Device is running.");
        InputBufferReader inputReader(iqPipeline, ringBuffer);
        SampleStream<SamplerType>().read(inputReader);
        log("[Stream1090] Shutting down device.");
        m_device->close();
        log("[Stream1090] Shutdown completed.");
        std::exit(0);
    }

    void run_sync_stdin(auto& iqPipeline) {
        log("[Stream1090] Reading from stdin");
        InputStdStreamReader<RawType, SamplerType::InputBufferSize, decltype(iqPipeline)> inputReader(iqPipeline, std::cin);
        SampleStream<SamplerType>().read(inputReader);
        log("[Stream1090] Finished.");
        std::exit(0);
    }

    void run() {
        // setup pipeline
        auto iqPipeline = IQPipelineSelector<inputRate, outputRate, pipelineOption>().make(m_runtimeVars.filterTaps);
        log(iqPipeline.toString());
        // for sync read from std in we take a short cut
        if (m_runtimeVars.deviceType == InputDeviceType::STREAM) {
            log("[Stream1090] Sync Stdin Mode");
            run_sync_stdin(iqPipeline);
        } else {
            log("[Stream1090] Async Device Mode");
            run_async_device(iqPipeline);
        }
    }

    void log(const std::string& str) {
        if (m_runtimeVars.verbose) {
            if (!str.ends_with('\n'))
                std::cerr << str << std::endl;
            else
                std::cerr << str;
        }
    }
private:
    
    DevicePtr m_device = nullptr;
    RuntimeVars m_runtimeVars;
};

template<typename Tuple, typename F>
constexpr bool for_each_in_tuple(const Tuple& t, F&& f) {
    bool done = false;
    std::apply([&](auto const&... elems) {
        (( !done && f(elems) ? done = true : false ), ...);
    }, t);
    return done;
}

bool runInstanceFromPresets(const CompileTimeVars& compileTimeVars, const RuntimeVars& runtimeVars) {
    return for_each_in_tuple(presets, [&](auto const& p) {
        using R = typename std::decay_t<decltype(p)>::RawType;
        if constexpr (std::is_same_v<R, uint8_t>) {
            if (compileTimeVars.rawFormat != RawFormat::UINT_8_IQ)
                return false;
        } else if constexpr (std::is_same_v<R, uint16_t>) {
            if (compileTimeVars.rawFormat != RawFormat::UINT_16_IQ)
                return false;
        }

        if (p.inputRate      != compileTimeVars.inputRate ||
            p.outputRate     != compileTimeVars.outputRate ||
            p.pipelineOption != compileTimeVars.pipelineOption)
            return false;

        using P = typename std::decay_t<decltype(p)>;
        MainInstance<P>(runtimeVars).run();
        return true;
    });
}