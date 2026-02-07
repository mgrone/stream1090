/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once
#include <memory>
#include <string>

#include "devices/IniConfig.hpp"

#ifdef STREAM1090_HAVE_AIRSPY
#include "devices/AirspyDevice.hpp"
#endif

#ifdef STREAM1090_HAVE_RTLSDR
#include "devices/RtlSdrDevice.hpp"
#endif

enum class InputDeviceType {
    STREAM,
    AIRSPY,
    RTLSDR,
    NONE
};

template<typename RawType>
class DeviceFactory {
public:
    using BasePtr = std::unique_ptr<InputDeviceBase<RawType>>;
    static BasePtr create(InputDeviceType inputType,
                          SampleRate inputSampleRate,
                          IAsyncWriter<RawType>& writer);
};

// ------------------------------------------------------------
// uint16_t specialization (Airspy)
// ------------------------------------------------------------
template<>
inline DeviceFactory<uint16_t>::BasePtr
DeviceFactory<uint16_t>::create(InputDeviceType inputType,
                                SampleRate inputSampleRate,
                                IAsyncWriter<uint16_t>& writer)
{
    // mark this as used to surpress warnings when certain backends are not present
    (void)(inputSampleRate);
    (void)(writer);

    switch (inputType)
    {
#ifdef STREAM1090_HAVE_AIRSPY
        case InputDeviceType::AIRSPY:
            return std::make_unique<AirspyDevice>(inputSampleRate, writer);
#endif
        case InputDeviceType::STREAM:
        case InputDeviceType::NONE:
        default:
            return nullptr;
    }
}

// ------------------------------------------------------------
// uint8_t specialization (RTL-SDR)
// ------------------------------------------------------------
template<>
inline DeviceFactory<uint8_t>::BasePtr
DeviceFactory<uint8_t>::create(InputDeviceType inputType,
                               SampleRate inputSampleRate,
                               IAsyncWriter<uint8_t>& writer)
{
    // mark this as used to surpress warnings when certain backends are not present
    (void)(inputSampleRate);
    (void)(writer);
    
    switch (inputType)
    {
#ifdef STREAM1090_HAVE_RTLSDR
        case InputDeviceType::RTLSDR:
            return std::make_unique<RtlSdrDevice>(inputSampleRate, writer);
#endif
        case InputDeviceType::STREAM:
        case InputDeviceType::NONE:
        default:
            return nullptr;
    }
}



