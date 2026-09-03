/* SPDX-License-Identifier: GPL-3.0-or-later */

// Combinations the device would not apply in full must be rejected. The case
// that motivated the check: 'gain' and the per-stage gains drive the same
// tuner in two alternative ways, and used together the per-stage ones always
// win, silently and whatever their order in the file, because
// IniConfig::Section is a std::map and settings are applied in alphabetical
// order.

#include "devices/RtlSdrDevice.hpp"

#include <cstdlib>
#include <cstdio>

namespace {

int failures = 0;

void check(const char* what, bool got, bool want) {
    if (got != want) {
        std::printf("FAIL %s: expected %s, got %s\n", what,
                    want ? "accepted" : "rejected",
                    got ? "accepted" : "rejected");
        failures++;
    }
}

IniConfig::Section section(std::initializer_list<std::pair<const char*, const char*>> kv) {
    IniConfig::Section s{{"serial", "00000001"}, {"frequency", "1090000000"}};
    for (auto& [k, v] : kv)
        s[k] = v;
    return s;
}

bool validate(const IniConfig::Section& s) {
    return RtlSdrDevice::validateCombinations(s);
}

} // namespace

int main() {
    // Valid forms: one of the two, never both.
    check("frequency and serial only", validate(section({})), true);
    check("gain only", validate(section({{"gain", "49.6"}})), true);
    check("gain with agc", validate(section({{"gain", "49.6"}, {"agc", "true"}})), true);

#if defined(STREAM1090_HAVE_RTLSDR_BLOG)
    check("three per-stage indices",
          validate(section({{"lna_gain", "15"}, {"mixer_gain", "15"}, {"vga_gain", "8"}})), true);
    check("three indices at the ends of the range",
          validate(section({{"lna_gain", "0"}, {"mixer_gain", "15"}, {"vga_gain", "0"}})), true);

    // All three or none: naming only some of them leaves the other stages on
    // hardware AGC, so the overall gain is not the one the written values
    // suggest.
    check("lna_gain only", validate(section({{"lna_gain", "15"}})), false);
    check("mixer_gain only", validate(section({{"mixer_gain", "15"}})), false);
    check("vga_gain only", validate(section({{"vga_gain", "8"}})), false);
    check("two out of three",
          validate(section({{"lna_gain", "15"}, {"vga_gain", "8"}})), false);

    // The conflict, with each of the three stages.
    check("gain + lna_gain",
          validate(section({{"gain", "49.6"}, {"lna_gain", "15"}})), false);
    check("gain + mixer_gain",
          validate(section({{"gain", "49.6"}, {"mixer_gain", "15"}})), false);
    check("gain + vga_gain",
          validate(section({{"gain", "49.6"}, {"vga_gain", "8"}})), false);
    check("gain + all three",
          validate(section({{"gain", "49.6"}, {"lna_gain", "15"},
                            {"mixer_gain", "15"}, {"vga_gain", "8"}})), false);

    // Out of range indices. 335 is the value one gets by reading the commented
    // out, tenths-of-a-dB version of r82xx_set_lna_gain: the live function
    // takes indices 0-15 and returns -1 for everything else.
    // Complete triples with a single invalid value, so that the rejection is
    // attributable to the range and not to an incomplete triple.
    auto triple = [](const char* l, const char* m, const char* v) {
        return section({{"lna_gain", l}, {"mixer_gain", m}, {"vga_gain", v}});
    };
    check("lna_gain = 335", validate(triple("335", "15", "8")), false);
    check("vga_gain = 16", validate(triple("15", "15", "16")), false);
    check("negative mixer_gain", validate(triple("15", "-1", "8")), false);
    check("non numeric index", validate(triple("15", "15", "high")), false);
    check("index with trailing text", validate(triple("15", "15", "8dB")), false);
    // "vga_gain =" with nothing after it. std::stoi throws on the empty
    // string, so the range test alone would compare 0 consumed characters
    // against a length of 0 and take it for a valid index 0.
    check("empty index", validate(triple("15", "15", "")), false);
    check("whitespace index", validate(triple("15", "15", " ")), false);
#else
    // Without the vendored library the three stages do not exist: they must
    // always be rejected, or the user believes they have set them.
    check("per-stage without the blog fork", validate(section({{"lna_gain", "15"}})), false);
    check("per-stage without the blog fork, valid value",
          validate(section({{"vga_gain", "8"}})), false);
#endif

    if (failures) {
        std::printf("%d tests failed\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
