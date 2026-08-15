#include "ModeS.hpp"

namespace {

bool decodesTo(uint16_t bits, int32_t expected) {
    const auto altitude = ModeS::decodeAltitude(bits);
    return altitude && *altitude == expected;
}

} // namespace

int main() {
    // Q=1 binary encoding, including a valid negative altitude.
    if (!decodesTo(0x0010, -1000)) return 1;
    if (!decodesTo(0x06b8, 10000)) return 2;

    // Q=0 Gillham / Mode C encoding.
    if (!decodesTo(0x0100, -1200)) return 3;
    if (!decodesTo(0x040a, 0)) return 4;
    if (!decodesTo(0x06a2, 10000)) return 5;
    if (!decodesTo(0x0e8b, 40000)) return 6;

    if (ModeS::decodeAltitude(0x0000)) return 7; // Invalid Gillham code.
    if (ModeS::decodeAltitude(0x0040)) return 8; // M=1 is not implemented.
    return 0;
}
