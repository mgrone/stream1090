/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once
#include "Global.hpp"
#include "Logger.hpp"
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
#include <chrono>
#include <optional>
#include <sstream>
#include <unistd.h>


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
    InputFormatType rawFormat = InputFormatType::IQ_UINT8_RTL_SDR;
    SampleRate inputRate = Rate_2_4_Mhz;
    SampleRate outputRate = Rate_8_0_Mhz;
    IQPipelineOptions pipelineOption = IQPipelineOptions::NONE;
};

struct RuntimeVars {
    InputDeviceType deviceType = InputDeviceType::STREAM;
    IniConfig deviceConfig;
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
    using RawFormatType = typename preset::RawFormatType;
    using RawType       = typename preset::RawType;
    using SamplerType   = typename preset::SamplerType;

    static constexpr SampleRate inputRate  = SamplerType::InputSampleRate;
    static constexpr SampleRate outputRate = SamplerType::OutputSampleRate;
    static constexpr IQPipelineOptions pipelineOption = preset::pipelineOption;

    // with all the compile time information available we continue now with what we need
    using DevicePtr   = std::unique_ptr<InputDeviceBase<RawType>>;
    using RingBuffer  = RingBufferAsync<RawType, SamplerType::InputBufferSize * 2>;
    using Writer      = typename RingBuffer::Writer;
    
    bool reloadDeviceConfig() {
        // Re-read the INI file from disk
        if (!m_runtimeVars.deviceConfig.reload()) {
            Log::error("Stream1090", "Failed to reload INI file.");
            return false;
        }

        auto& cfg = m_runtimeVars.deviceConfig.get();

        // Extract the correct section
        if (m_runtimeVars.deviceType == InputDeviceType::AIRSPY) {
            if (!cfg.count("airspy")) {
                Log::error("Stream1090", "Reloaded INI missing [airspy] section.");
                return false;
            }
            m_runtimeVars.deviceConfigSection = cfg.at("airspy");
        }

        else if (m_runtimeVars.deviceType == InputDeviceType::RTLSDR) {
            if (!cfg.count("rtlsdr")) {
                Log::error("Stream1090", "Reloaded INI missing [rtlsdr] section.");
                return false;
            }
            m_runtimeVars.deviceConfigSection = cfg.at("rtlsdr");
        }

        return true;
    }

    bool setup_device() {
        const auto& cfg = m_runtimeVars.deviceConfigSection;
        
        // tell the device to read all properties required to open it
        Log::info("Stream1090","Reading initial properties from the ini file");
        m_device->applyConfigPreOpen(cfg);

        
        // let us try to open the device
        Log::info("Stream1090","Trying to open the device.");
        if (!m_device->open()) {
            Log::error("Stream1090","Opening device failed.");
            // this is not good at all
            return false;
        };

        // device is ready, apply all the other properties
        Log::info("Stream1090","Device is open. Reading the ini file.");
        m_device->applyConfigPostOpen(cfg);
        
        // we do not care if any of the properties did not work
        return true;
   }

    static constexpr auto constructMessageHandler(SampleStream<SamplerType>& sampleStream) {
        if constexpr(GlobalOptions::RSSIEnabled) {
            return RssiStdOutMessageHandler<SamplerType, SampleStream<SamplerType> >(sampleStream);
        } else {
            return StdOutMessageHandler<SamplerType>();
        }
    }

    bool run_async_device(auto& iqPipeline) {
        RingBuffer ringBuffer;
        Writer writer(ringBuffer);

        m_device = DeviceFactory<RawType>::create(m_runtimeVars.deviceType, inputRate, writer);
        if (!m_device) {
            Log::error("Stream1090", "Device instantiation failed.");
            return false;
        }
        Log::info("Stream1090", "Device created.");

        if (!setup_device()) {
            Log::error("Stream1090", "Device configuration failed.");
            return false;
        }
        Log::info("Stream1090", "Device successfully configured.");

        if (!m_device->start()) {
            Log::error("Stream1090", "Device refuses to start. Aborting.");
            return false;
        }
        Log::info("Stream1090", "Device is running. ");

        Log::info("Stream1090", "Installing sig handlers.");
        ProcessSignals::install();

        // If we made it until here, we assume that this device is ready and alive.
        // We mark it here as such, because especially the rtlsdr driver needs some
        // time to startup which sometimes may exceed the timeout of the watchdog.
        // In other words, the driver is still initializing, but takes so long that
        // the watchdog thinks it is dead and tries to kill it.
        m_device->markAsAlive();
        Log::info("Stream1090", "Devices has been marked as alive.");

        // -------------------------------
        // WATCHDOG THREAD
        // -------------------------------
        std::thread watchdog([this] {
            using namespace std::chrono_literals;
            Log::info("Watchdog", "Started.");
            while (!ProcessSignals::shutdownRequested()) {
                // 1) Device health check. Is the device still alive?
                if (m_device && m_device->lastSignOfLife() > 1000ms) {
                    Log::error("Watchdog", "No samples for 1000ms. Device lost? Initiating shutdown.");
                    // Only wake the pipeline here, and leave the device to the
                    // shutdown path below, which closes it in every case.
                    // Closing from this thread as well means two threads run
                    // close() concurrently: both reach rtlsdr_close/airspy_close
                    // on the same handle and both join the same reader thread.
                    m_device->shutdownWriter();
                    ProcessSignals::handle_sigint(0);
                    break;
                }

                // 2) Device health check. Issue a warning if the device falls behind.
                if (m_device && m_device->lastSignOfLife() > 200ms) {
                    Log::warn("Watchdog", "No samples for more than 200ms. The device is falling behind.");
                }

                // 3) Reload request (SIGHUP)
                if (ProcessSignals::reloadRequested()) {
                    ProcessSignals::clearReload();
                    Log::info("Stream1090", "Re-reading config file.");

                    if (reloadDeviceConfig()) {
                        Log::info("Stream1090", "Applying new configuration.");
                        m_device->applyConfigPostOpen(m_runtimeVars.deviceConfigSection);
                    } else {
                        Log::warn("Stream1090", "Reload failed. Keeping old settings.");
                    }
                }

                std::this_thread::sleep_for(200ms);
            }
            Log::info("Watchdog", "Watchdog is done.");
        });


        // -------------------------------
        // DSP PIPELINE (blocking)
        // -------------------------------
        auto start_wct = std::chrono::steady_clock::now();

        if (m_device->isRunning()) {
            Log::info("Stream1090", "Device is running, starting stream.");
            InputBufferReader<
                RawFormatType,
                SamplerType::InputBufferSize * 2,
                8,
                decltype(iqPipeline)
            > inputReader(iqPipeline, ringBuffer);

            SampleStream<SamplerType> sampleStream;
            auto messageHandler = constructMessageHandler(sampleStream);
            
            sampleStream.read(inputReader, messageHandler);
        }

        // -------------------------------
        // SHUTDOWN
        // -------------------------------
        Log::info("Stream1090", "Shutting down device.");
        m_device->close();
        Log::info("Stream1090", "Device closed down.");

        auto end_wct = std::chrono::steady_clock::now();
        auto dur_wct_secs = std::chrono::duration_cast<std::chrono::milliseconds>(end_wct - start_wct).count();
        if (watchdog.joinable()) {
            Log::info("Stream1090", "Watchdog joining.");
            watchdog.join();
            Log::info("Stream1090", "Watchdog joined.");
        }
        Log::info("Stream1090", "Shutdown completed.");
        Log::msg("Stream1090") << "Finished. (" << dur_wct_secs/1000.0 << "s)";
        return true;
    }


    bool run_sync_stdin(auto& iqPipeline) {
        Log::info("Stream1090", "Reading from stdin");
        auto start_wct = std::chrono::steady_clock::now();

        InputStdStreamReader<
            RawFormatType,
            SamplerType::InputBufferSize,
            decltype(iqPipeline)
        > inputReader(iqPipeline, STDIN_FILENO);

        
        SampleStream<SamplerType> sampleStream;
        auto messageHandler = constructMessageHandler(sampleStream);
        sampleStream.read(inputReader, messageHandler);

        auto end_wct = std::chrono::steady_clock::now();
        auto dur_wct_secs = std::chrono::duration_cast<std::chrono::milliseconds>(end_wct - start_wct).count();
        Log::msg("Stream1090") << "Finished. (" << dur_wct_secs/1000.0 << "s)";
        return true;
    }

    bool run() {
        // setup pipeline
        auto iqPipeline = IQPipelineSelector<inputRate, outputRate, pipelineOption>().make(m_runtimeVars.filterTaps);
        Log::info("",iqPipeline.toString());
        // for sync read from std in we take a short cut
        if (m_runtimeVars.deviceType == InputDeviceType::STREAM) {
            Log::info("Stream1090", "Sync Stdin Mode");
            return run_sync_stdin(iqPipeline);
        } else {
            Log::info("Stream1090", "Async Device Mode");
            return run_async_device(iqPipeline);
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

// Returns nothing when no preset matches the requested configuration, and
// otherwise the outcome of the run: true when the instance ran and shut down
// normally, false when it gave up (for example because the device never came up).
std::optional<bool> runInstanceFromPresets(const CompileTimeVars& compileTimeVars, const RuntimeVars& runtimeVars) {
    std::optional<bool> outcome;
    for_each_in_tuple(presets, [&](auto const& p) {
        using P = std::decay_t<decltype(p)>;

        if (P::RawFormatType::id  == compileTimeVars.rawFormat &&
            P::inputRate          == compileTimeVars.inputRate &&
            P::outputRate         == compileTimeVars.outputRate &&
            P::pipelineOption     == compileTimeVars.pipelineOption) 
        {
            outcome = MainInstance<P>(runtimeVars).run();
            return true;
        }
        return false;
    });
    return outcome;
}
