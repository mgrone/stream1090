/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#include <iostream>
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <optional>

#define STREAM1090_VERSION "260208"

#include "MainInstance.hpp"


struct RatePair {
    SampleRate in;
    SampleRate out;
};

std::vector<RatePair> collect_rate_pairs() {
    std::vector<RatePair> pairs;
    std::apply([&](auto... p) {
        ((pairs.push_back({ decltype(p)::inputRate, decltype(p)::outputRate })), ...);
    }, presets);

    // Sort by input, then output
    std::sort(pairs.begin(), pairs.end(),
        [](auto& a, auto& b) {
            if (a.in != b.in) return (int)a.in < (int)b.in;
            return (int)a.out < (int)b.out;
        });

    // Remove duplicates
    pairs.erase(std::unique(pairs.begin(), pairs.end(),
        [](auto& a, auto& b) {
            return a.in == b.in && a.out == b.out;
        }), pairs.end());

    return pairs;
}

std::optional<SampleRate> find_default_output_rate(SampleRate input) {
    auto pairs = collect_rate_pairs();
    for (auto& p : pairs) {
        if (p.in == input)
            return p.out;   // first match
    }
    return std::nullopt;
}

bool is_valid_rate_pair(SampleRate in, SampleRate out) {
    auto pairs = collect_rate_pairs();
    for (auto& p : pairs) {
        if (p.in == in && p.out == out)
            return true;
    }
    return false;
}


void print_rate_pairs() {
    auto pairs = collect_rate_pairs();

    std::cout << "Supported sample rate combinations:\n";
    for (auto& p : pairs) {
        std::string fmt = (p.in < 6'000'000) ? "uint8 IQ" : "uint16 IQ";
        std::cout
            << "  "
            << (float(p.in) / 1'000'000.0f)
            << "  →  "
            << (float(p.out) / 1'000'000.0f)
            <<  " (" << fmt << ")\n";
    }
    std::cout << "\n";
}

void print_help() {
    std::cout << "Stream1090 build " << STREAM1090_VERSION << "\n";

    std::cout << "Native device support:";
#ifdef STREAM1090_HAVE_AIRSPY
    std::cout << " Airspy";
#endif
#ifdef STREAM1090_HAVE_RTLSDR
    std::cout << " RTL-SDR";
#endif
#if !defined(STREAM1090_HAVE_AIRSPY) && !defined(STREAM1090_HAVE_RTLSDR)
    std::cout << " none";
#endif
    std::cout << "\n\n";

    std::cout <<
    "Usage:\n"
    "  stream1090 [options]\n\n"
    "Options:\n"
    "  -s <rate>            Input sample rate in MHz (required)\n"
    "  -u <rate>            Output/upsample rate in MHz\n"
    "  -d <file.ini>        Device configuration INI file for native devices\n"
    "                       See config/airspy.ini or config/rtlsdr.ini\n"
    "                       Note that native device support requires librtlsdr-dev\n"
    "                       and/or libairspy-dev to be installed.\n"
    "  -q                   Enables IQ FIR filter with built-in taps (default or custom)\n"
    "  -f <taps file>       Taps to load that are used for the IQ FIR filter\n"
    "  -v                   Verbose output\n"
    "  -h, --help           Show this help message\n\n";

    print_rate_pairs();
    
    std::cout <<
    "Examples:\n"
    "  rtl_sdr -g 0 -f 1090000000 -s 2400000 - | ./build/stream1090 -s 2.4 -u 8\n"
    "  ./build/stream1090 -s 2.4 -u 8 -d ./configs/rtlsdr.ini\n"
    "\n"
    "  airspy_rx -t 4 -g 20 -f 1090.000 -a 12000000 -r - | ./build/stream1090 -s 6 -u 12 -q\n"
    "  ./build/stream1090 -s 6 -u 12 -d ./configs/airspy.ini -q\n\n";
}


struct CliArgs {
    std::string sampleRate = "";
    std::string upsampleRate = "";
    std::string deviceConfig = "";
    std::string tapsFile = "";
    bool iq_filter = false;
    bool verbose = false;
};

bool parse_cli(int argc, char** argv, CliArgs& out) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_help();
            std::exit(0);
        }

        if (arg == "-s" && i + 1 < argc) {
            out.sampleRate = argv[++i];
            continue;
        }

        if (arg == "-u" && i + 1 < argc) {
            out.upsampleRate = argv[++i];
            continue;
        }

        if (arg == "-d" && i + 1 < argc) {
            out.deviceConfig = argv[++i];
            continue;
        }

        if (arg == "-f" && i + 1 < argc) {
            out.tapsFile = argv[++i];
            continue;
        }

        if (arg == "-q") {
            out.iq_filter = true;
            continue;
        }

        if (arg == "-v") {
            out.verbose = true;
            continue;
        }

        std::cerr << "Unknown or incomplete argument: " << arg << "\n";
        return false;
    }

    return true;
}

SampleRate parse_sample_rate(const std::string& raw) {
    // Strip optional trailing 'M' or 'm'
    std::string s = raw;
    if (!s.empty() && (s.back() == 'M' || s.back() == 'm'))
        s.pop_back();

    // Parse as float MHz
    float mhz = 0.0f;
    try {
        mhz = std::stof(s);
    } catch (...) {
        std::cerr << "Invalid sample rate: " << raw << "\n";
        std::exit(1);
    }

    // Convert MHz → Hz
    int hz = static_cast<int>(mhz * 1'000'000.0f + 0.5f);

    // Match directly against enum values
    switch (hz) {
        case Rate_1_0_Mhz:  return Rate_1_0_Mhz;
        case Rate_2_0_Mhz:  return Rate_2_0_Mhz;
        case Rate_2_4_Mhz:  return Rate_2_4_Mhz;
        case Rate_3_0_Mhz:  return Rate_3_0_Mhz;
        case Rate_3_2_Mhz:  return Rate_3_2_Mhz;
        case Rate_4_0_Mhz:  return Rate_4_0_Mhz;
        case Rate_6_0_Mhz:  return Rate_6_0_Mhz;
        case Rate_8_0_Mhz:  return Rate_8_0_Mhz;
        case Rate_10_0_Mhz: return Rate_10_0_Mhz;
        case Rate_12_0_Mhz: return Rate_12_0_Mhz;
        case Rate_16_0_Mhz: return Rate_16_0_Mhz;
        case Rate_20_0_Mhz: return Rate_20_0_Mhz;
        case Rate_24_0_Mhz: return Rate_24_0_Mhz;
        case Rate_40_0_Mhz: return Rate_40_0_Mhz;
        case Rate_48_0_Mhz: return Rate_48_0_Mhz;
    }

    std::cerr << "Unsupported sample rate: " << raw << "\n";
    std::exit(1);
}

std::vector<float> load_taps_from_file(const std::string& filename) {
    std::vector<float> taps;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return taps;
    }

    std::string line;
    while (std::getline(file, line)) {
        // trim whitespace
        if (line.empty()) continue;

        // skip comments
        if (line[0] == '#') continue;

        // parse float
        try {
            float v = std::stof(line);
            taps.push_back(v);
        } catch (...) {
            // malformed line
            return std::vector<float>();;
        }

        // too many taps
        if (taps.size() > 64) {
            return std::vector<float>();
        }
    }

    return taps;
}

/*std::vector<float> load_taps_from_file(const std::string& filename) {
    std::vector<float> taps;
    std::ifstream in(filename);

    if (!in) {
        std::cerr << "Failed to open taps file: " << filename << "\n";
        return taps;
    }

    float v;
    while (in >> v) {
        taps.push_back(v);
    }

    return taps;
}*/


int main(int argc, char** argv) {
    install_signal_handlers();
    RuntimeVars r_vars;
    CompileTimeVars c_vars;

    CliArgs args;
    if (!parse_cli(argc, argv, args)) {
        std::cerr << "Usage: stream1090 -s <rate> -u <rate> [-d <device.ini>] [-f <taps file>] [-q] [-v] [-h]\n";
        return 1;
    }

    if (args.sampleRate.empty()) {
        print_help();
        return 1;
    }

    IniConfig dev_ini;
    if (!args.deviceConfig.empty() && !dev_ini.load(args.deviceConfig)) {
        r_vars.deviceType = InputDeviceType::STREAM;
    } else {
        auto& cfg = dev_ini.get();   
        if (cfg.count("airspy")) {
            r_vars.deviceType = InputDeviceType::AIRSPY;
            r_vars.deviceConfigSection = cfg.at("airspy");
        } else if (cfg.count("rtlsdr")) {
            r_vars.deviceType = InputDeviceType::RTLSDR;
            r_vars.deviceConfigSection = cfg.at("rtlsdr");
        } else {
            r_vars.deviceType = InputDeviceType::STREAM;
        }
    }

    if (!args.tapsFile.empty()) {
        r_vars.filterTaps = load_taps_from_file(args.tapsFile);
        if (r_vars.filterTaps.empty()) {
            std::cerr << "Error loading taps from " << args.tapsFile << std::endl;
            return 1;
        }
    }

    r_vars.verbose = args.verbose;

    c_vars.inputRate = parse_sample_rate(args.sampleRate);

    // Case 1: user provided both -s and -u
    if (!args.upsampleRate.empty()) {
        c_vars.outputRate = parse_sample_rate(args.upsampleRate);

        if (!is_valid_rate_pair(c_vars.inputRate, c_vars.outputRate)) {
            std::cerr << "[Stream1090] Unsupported rate combination: "
                    << float(c_vars.inputRate)/1'000'000.0f << " → "
                    << float(c_vars.outputRate)/1'000'000.0f << "\n";
            print_rate_pairs();
            return 1;
        }
    }
    // Case 2: user provided only -s
    else {
        auto def = find_default_output_rate(c_vars.inputRate);
        if (!def) {
            std::cerr << "[Stream1090] No valid output rate for input rate: "
                    << float(c_vars.inputRate)/1'000'000.0f << "\n";
            print_rate_pairs();
            return 1;
        }

        c_vars.outputRate = *def;

        if (args.verbose) {
            std::cout << "[Stream1090] Auto-selected output rate: "
                    << float(c_vars.outputRate)/1'000'000.0f << " MHz\n";
        }
    }

    // TODO: Change this
    c_vars.rawFormat = (c_vars.inputRate < Rate_6_0_Mhz)
        ? RawFormat::UINT_8_IQ
        : RawFormat::UINT_16_IQ;

    c_vars.pipelineOption = IQPipelineOptions::NONE;
    if (!r_vars.filterTaps.empty())
        c_vars.pipelineOption = IQPipelineOptions::IQ_FIR_FILE;
    else if (args.iq_filter)
        c_vars.pipelineOption = IQPipelineOptions::IQ_FIR;
        
    if (!runInstanceFromPresets(c_vars, r_vars)) {
        std::cerr << "[Stream1090] Configuration is not supported: "<< c_vars.inputRate << " -> " << c_vars.outputRate << std::endl;
        return -1;
    }
    return 0;
}






