/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "devices/InputDeviceBase.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

void requireImpl(bool condition, int line) {
    if (!condition) {
        std::fprintf(stderr, "requirement failed at line %d\n", line);
        std::abort();
    }
}

#define require(condition) requireImpl((condition), __LINE__)

class NullWriter : public IAsyncWriter<int> {
public:
    size_t write(const int*, size_t n) override { return n; }
    void shutdown() override {}
};

class TestDevice : public InputDeviceBase<int> {
public:
    explicit TestDevice(IAsyncWriter<int>& writer)
        : InputDeviceBase<int>(Rate_1_0_Mhz, writer) {}

    bool open() override { return true; }
    bool start() override { return true; }
    void stop() override {}
    void close() override {}

    bool applySetting(const std::string&, const std::string& value) override {
        if (value == "invalid")
            throw std::invalid_argument("invalid test value");
        return value == "accepted";
    }
};

void checkBoolean(const char* value, bool expectedParsed, bool expectedValue) {
    bool out = !expectedValue;
    const bool parsed = DeviceSettings::parseBoolean(value, out);
    require(parsed == expectedParsed);
    if (expectedParsed)
        require(out == expectedValue);
}

} // namespace

int main() {
    NullWriter writer;
    TestDevice device(writer);

    require(device.applySettingSafely("gain", "accepted"));
    require(!device.applySettingSafely("gain", "rejected"));
    require(!device.applySettingSafely("gain", "invalid"));

    int integer = 0;
    uint32_t unsignedValue = 0;
    float floating = 0.0f;
    require(DeviceSettings::parseInt("-12", integer) && integer == -12);
    require(!DeviceSettings::parseInt("", integer));
    require(!DeviceSettings::parseInt("12dB", integer));
    require(!DeviceSettings::parseInt("99999999999999999999", integer));
    require(DeviceSettings::parseUnsigned("1090000000", unsignedValue)
            && unsignedValue == 1090000000);
    require(!DeviceSettings::parseUnsigned("", unsignedValue));
    require(!DeviceSettings::parseUnsigned("-1", unsignedValue));
    require(!DeviceSettings::parseUnsigned("4294967296", unsignedValue));
    require(DeviceSettings::parseFloat("49.6", floating)
            && floating == 49.6f);
    require(DeviceSettings::parseFloat("1e2", floating)
            && floating == 100.0f);
    require(!DeviceSettings::parseFloat("", floating));
    require(!DeviceSettings::parseFloat("49.6dB", floating));
    require(!DeviceSettings::parseFloat("nan", floating));
    require(!DeviceSettings::parseFloat("inf", floating));

    // The six spellings a boolean setting takes.
    checkBoolean("1", true, true);
    checkBoolean("true", true, true);
    checkBoolean("on", true, true);
    checkBoolean("0", true, false);
    checkBoolean("false", true, false);
    checkBoolean("off", true, false);

    // Everything else is a configuration error, not a silent false. "yes" is
    // the one people actually write; the rest are the near misses that used to
    // read as "off" just as quietly.
    for (const char* rejected : { "yes", "no", "y", "n", "enabled", "disabled",
                                  "True", "TRUE", "On", "OFF", "2", "-1",
                                  "", " ", "true ", " true", "tru" })
        checkBoolean(rejected, false, false);

    // The reporting wrapper agrees with the quiet one, and leaves the output
    // untouched when it refuses.
    bool out = true;
    require(!DeviceSettings::parseBooleanOrReport("agc", "yes", out));
    require(out);
    require(DeviceSettings::parseBooleanOrReport("agc", "off", out));
    require(!out);
}
