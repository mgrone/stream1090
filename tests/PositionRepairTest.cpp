/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "DemodCore.hpp"

#include <array>
#include <cstdint>

namespace {

struct CapturingHandler {
	void handleShort(uint64_t, uint64_t) {}

	void handleLong(uint64_t, const Bits128& frame) {
		if (longCount < frames.size())
			frames[longCount] = frame;
		++longCount;
	}

	uint32_t longCount { 0 };
	std::array<Bits128, 8> frames { Bits128(), Bits128(), Bits128(),
		Bits128(), Bits128(), Bits128(), Bits128(), Bits128() };
};

Bits128 makePosition(uint32_t icao, bool odd, uint32_t latCpr,
		uint32_t lonCpr, uint8_t capability = 5) {
	const uint64_t high = (uint64_t(17) << 43)
		| (uint64_t(capability) << 40)
		| (uint64_t(icao) << 16)
		| (uint64_t(11) << 11);
	const uint64_t low = (uint64_t(odd) << 58)
		| (uint64_t(latCpr) << 41)
		| (uint64_t(lonCpr) << 24);
	Bits128 frame(high, low);
	frame.low() |= CRC::compute<112>(frame);
	return frame;
}

Bits128 makeIdentity(uint32_t icao, uint8_t capability = 5) {
	const uint64_t high = (uint64_t(17) << 43)
		| (uint64_t(capability) << 40)
		| (uint64_t(icao) << 16)
		| (uint64_t(5) << 11);
	Bits128 frame(high, 0);
	frame.low() = CRC::compute<112>(frame);
	return frame;
}

void feedSilence(DemodCore<1, CapturingHandler>& demod, uint32_t bits) {
	for (uint32_t bit = 0; bit < bits; ++bit) {
		uint32_t value[] = { 0 };
		demod.shiftInNewBits(value);
	}
}

void feedFrame(DemodCore<1, CapturingHandler>& demod, const Bits128& frame) {
	for (int bit = 111; bit >= 0; --bit) {
		uint32_t value[] = { uint32_t(frame.get(bit)) };
		demod.shiftInNewBits(value);
	}
	feedSilence(demod, 16);
}

} // namespace

int main() {
	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);

	// A repaired position for a trusted aircraft with no clean odd/even pair
	// behind it must be rejected (repairs never establish the first position).
	// 0x12345 becomes trusted via a clean identity frame first.
	feedFrame(demod, makeIdentity(0x123456));
	feedSilence(demod, 128);
	auto firstRepair = makePosition(0x123456, false, 93000, 51372);
	firstRepair.flip(0);
	feedSilence(demod, 128);
	feedFrame(demod, firstRepair);
	if (handler.longCount != 0)
		return 1;

	// A CRC-clean even/odd pair establishes the reference position; both
	// frames are emitted.
	const auto firstEven = makePosition(0x123456, false, 93000, 51372);
	feedFrame(demod, firstEven);
	feedSilence(demod, 128);
	const auto firstOdd = makePosition(0x123456, true, 74158, 50194);
	feedFrame(demod, firstOdd);
	if (handler.longCount != 2)
		return 2;

	// A single-bit damage repair landing near the established position passes.
	auto nearby = makePosition(0x123456, false, 93000, 51372);
	nearby.flip(0);
	feedSilence(demod, 128);
	feedFrame(demod, nearby);
	if (handler.longCount != 3)
		return 3;

	// A repair that would place the aircraft > 100 km away is rejected.
	auto farAway = makePosition(0x123456, false, 0, 0);
	farAway.flip(0);
	feedSilence(demod, 128);
	feedFrame(demod, farAway);

	return handler.longCount != 3 ? 4 : 0;
}