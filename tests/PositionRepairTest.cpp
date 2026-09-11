/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "DemodCore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

struct CapturingHandler {
	void handleShort(uint64_t, uint64_t) {}

	void handleLong(uint64_t sampleTime, const Bits128& frame) {
		lastLong = frame;
		++longCount;
		longTimes.push_back(sampleTime);
		longFrames.push_back(frame);
	}

	uint32_t longCount { 0 };
	Bits128 lastLong;
	// Every timestamp and frame actually sent to the output, in emission
	// order. The gate must never buffer, so this doubles as the record of
	// what a downstream receiver would have seen.
	std::vector<uint64_t> longTimes;
	std::vector<Bits128> longFrames;
};

// The gate promises no buffering and no retroactive emission: output
// timestamps must be strictly increasing, and a frame withheld once must
// never appear later just because a subsequent frame satisfied the gate.
bool outputTimestampsAreMonotonic(const CapturingHandler& handler) {
	for (size_t i = 1; i < handler.longTimes.size(); ++i) {
		if (handler.longTimes[i] <= handler.longTimes[i - 1])
			return false;
	}
	return true;
}

bool frameWasNeverEmitted(const CapturingHandler& handler, const Bits128& frame) {
	for (const auto& emitted : handler.longFrames) {
		if (emitted == frame)
			return false;
	}
	return true;
}

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

Bits128 makeIdentification(uint32_t icao, uint8_t meType = 1,
		uint8_t capability = 5) {
	const uint64_t high = (uint64_t(17) << 43)
		| (uint64_t(capability) << 40)
		| (uint64_t(icao) << 16)
		| (uint64_t(meType) << 11);
	Bits128 frame(high, 0);
	frame.low() = CRC::compute<112>(frame);
	return frame;
}

template<int N, typename H>
void feedSilenceN(DemodCore<N, H>& demod, uint32_t ticks) {
	uint32_t value[N];
	std::fill(std::begin(value), std::end(value), 0u);
	for (uint32_t tick = 0; tick < ticks; ++tick)
		demod.shiftInNewBits(value);
}

void feedSilence(DemodCore<1, CapturingHandler>& demod, uint32_t bits) {
	feedSilenceN(demod, bits);
}

template<int N, typename H>
void feedFrameLongN(DemodCore<N, H>& demod, const Bits128& frame) {
	for (int bit = 111; bit >= 0; --bit) {
		uint32_t value[N];
		std::fill(std::begin(value), std::end(value), uint32_t(frame.get(bit)));
		demod.shiftInNewBits(value);
	}
	feedSilenceN(demod, 16);
}

void feedFrame(DemodCore<1, CapturingHandler>& demod, const Bits128& frame) {
	feedFrameLongN(demod, frame);
}

// One microsecond of raw bit stream per shiftInNewBits() call, regardless of
// NumStreams: each call feeds NumStreams parallel phase-candidates for that
// same microsecond, so the tick-count arguments used throughout this file
// (frame lengths, pairing windows, silences) are valid unchanged at any
// NumStreams. Broadcasting an identical bit to every stream makes all
// NumStreams phases converge on the same content; only the lowest-indexed
// stream actually reaches the dispatcher; the rest are filtered as phase
// duplicates, exactly as real hardware would collapse redundant detections.
template<int N, typename H>
void feedFrameShortN(DemodCore<N, H>& demod, uint64_t frame) {
	for (int bit = 55; bit >= 0; --bit) {
		uint32_t value[N];
		std::fill(std::begin(value), std::end(value), uint32_t((frame >> bit) & 1));
		demod.shiftInNewBits(value);
	}
	feedSilenceN(demod, 16);
}

uint64_t makeDF11(uint32_t icao, uint8_t capability = 5) {
	// interrogatorCode 0 keeps the CRC clean (parity == checksum), taking the
	// simplest DF11 accept path so short-frame traffic here needs no separate
	// two-sighting corroboration beyond what the address already has.
	uint64_t frame = (uint64_t(11) << 51)
		| (uint64_t(capability) << 48)
		| (uint64_t(icao) << 24);
	return frame | CRC::compute<56>(Bits128(frame));
}

// Damaged so that the error table repair recovers the frame itself: the
// single flipped parity bit is what the repair undoes.
Bits128 makeRepairable(const Bits128& frame) {
	Bits128 damaged { frame };
	damaged.flip(0);
	return damaged;
}

void decodeCprRelativeForTest(double refLat, double refLon, bool odd,
		uint32_t latCpr, uint32_t lonCpr, double& lat, double& lon) {
	const double dlat = odd ? 360.0 / 59.0 : 360.0 / 60.0;
	const double normalizedLat = double(latCpr) / 131072.0;
	lat = dlat * (std::floor(0.5 + refLat / dlat - normalizedLat)
		+ normalizedLat);
	if (lat > 90.0) lat -= 180.0;
	if (lat < -90.0) lat += 180.0;

	const int zones = ModeS::cprNl(lat) - (odd ? 1 : 0);
	const int ni = zones > 1 ? zones : 1;
	const double dlon = 360.0 / double(ni);
	const double normalizedLon = double(lonCpr) / 131072.0;
	lon = dlon * (std::floor(0.5 + refLon / dlon - normalizedLon)
		+ normalizedLon);
	if (lon > 180.0) lon -= 360.0;
	if (lon < -180.0) lon += 360.0;
}

double distanceKmForTest(double lat1, double lon1, double lat2, double lon2) {
	constexpr double EarthRadiusKm = 6371.0;
	constexpr double DegreesToRadians = 3.14159265358979323846 / 180.0;
	const double deltaLat = (lat2 - lat1) * DegreesToRadians;
	const double deltaLon = (lon2 - lon1) * DegreesToRadians;
	const double a = std::sin(deltaLat * 0.5) * std::sin(deltaLat * 0.5)
		+ std::cos(lat1 * DegreesToRadians) * std::cos(lat2 * DegreesToRadians)
		* std::sin(deltaLon * 0.5) * std::sin(deltaLon * 0.5);
	return 2.0 * EarthRadiusKm * std::asin(std::sqrt(a));
}

bool fixturesHaveExpectedGeometry() {
	double refLat = 0.0;
	double refLon = 0.0;
	if (!ModeS::decodeCprGlobal(93000, 51372, 74158, 50194,
			true, refLat, refLon))
		return false;

	double localLat = 0.0;
	double localLon = 0.0;
	decodeCprRelativeForTest(refLat, refLon, false, 76616, 51372,
		localLat, localLon);
	const double localGhostKm = distanceKmForTest(
		refLat, refLon, localLat, localLon);

	double globalLat = 0.0;
	double globalLon = 0.0;
	if (!ModeS::decodeCprGlobal(76616, 51372, 74158, 50194,
			false, globalLat, globalLon))
		return false;
	const double globalGhostKm = distanceKmForTest(
		refLat, refLon, globalLat, globalLon);
	if (localGhostKm < 80.0 || localGhostKm > 90.0
			|| globalGhostKm < 4'000.0)
		return false;

	if (!ModeS::decodeCprGlobal(53521, 47623, 65736, 73400,
			true, refLat, refLon))
		return false;
	decodeCprRelativeForTest(refLat, refLon, false, 53521, 47623,
		localLat, localLon);
	const double localSouthKm = distanceKmForTest(
		refLat, refLon, localLat, localLon);
	decodeCprRelativeForTest(refLat, refLon, false, 0, 0,
		localLat, localLon);
	const double farSouthKm = distanceKmForTest(
		refLat, refLon, localLat, localLon);
	return localSouthKm < 1.0 && farSouthKm > 300.0;
}

bool repairedPairCannotBypassGlobalGate(bool followingClean) {
	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);
	constexpr uint32_t icao = 0x345678;

	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 128);
	feedFrame(demod, makeIdentification(icao, 3));
	const auto cleanEven = makePosition(icao, false, 93000, 51372);
	const auto cleanOdd = makePosition(icao, true, 74158, 50194);
	feedFrame(demod, cleanEven);
	feedSilence(demod, 128);
	feedFrame(demod, cleanOdd);

	// Let both clean parities age out of the 10-second pairing window while
	// keeping their decoded reference and the aircraft trust alive.
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 3));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 1'500'000);

	// Locally near is insufficient: a later clean odd frame would pair with
	// this repair into a 4700 km ghost. It is withheld here for lack of a
	// fresh opposite parity too, and the next block must show it has no way
	// back in: no buffering, no retroactive emission once a later frame
	// satisfies the gate.
	const auto repairedEven = makePosition(icao, false, 76616, 51372);
	feedFrame(demod, makeRepairable(repairedEven));
	if (handler.longCount != 6)
		return false;

	feedFrame(demod, followingClean ? cleanOdd : makeRepairable(cleanOdd));
	if (!followingClean) {
		// Still no fresh opposite parity: also discarded, and the withheld
		// even repair from above has not surfaced.
		return handler.longCount == 6
			&& frameWasNeverEmitted(handler, repairedEven);
	}
	// The clean odd frame is emitted normally right on top of the discard,
	// and it is what went out -- not a delayed replay of repairedEven.
	if (handler.longCount != 7 || handler.lastLong != cleanOdd
			|| !frameWasNeverEmitted(handler, repairedEven))
		return false;

	// Once a clean opposite parity is available, valid repairs resume, and
	// the discard from before still never surfaces.
	feedFrame(demod, makeRepairable(cleanEven));
	return handler.longCount == 8 && handler.lastLong == cleanEven
		&& frameWasNeverEmitted(handler, repairedEven)
		&& outputTimestampsAreMonotonic(handler);
}

// A discarded repair must not overwrite the emitted-CPR history slot for its
// own parity: only an accepted send may do that. Proven with a repair whose
// CPR is bit-identical to the value already on record there, so the only
// thing a wrongful write could change is the *timestamp* -- and that is
// exactly what a later, otherwise-identical repair for the opposite parity
// would key off if the discard had (wrongly) refreshed it.
bool discardedRepairDoesNotRefreshEmittedHistory() {
	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);
	constexpr uint32_t icao = 0x445566;

	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 128);
	feedFrame(demod, makeIdentification(icao, 3));
	if (handler.longCount != 1)
		return false;

	const auto even = makePosition(icao, false, 93000, 51372);
	const auto odd = makePosition(icao, true, 74158, 50194);
	feedFrame(demod, even);
	feedSilence(demod, 128);
	feedFrame(demod, odd);
	if (handler.longCount != 3)
		return false;

	// Age both emitted parities out of the 10 s pairing window while
	// identifying frames keep the entry alive and its clean reference fresh.
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 3));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 1'500'000);
	if (handler.longCount != 6)
		return false;

	// Discarded: the odd side's emitted output is stale, so this repair --
	// even though it reconstructs the exact even value already on record --
	// cannot be confirmed.
	feedFrame(demod, makeRepairable(even));
	if (handler.longCount != 6)
		return false;

	// If that discard had refreshed the emitted-even slot's timestamp, this
	// odd repair would now see a fresh even partner (the same value, just a
	// newer stamp) and pair straight back to the reference. It must still be
	// rejected: the even slot the gate actually holds is the one set by the
	// original clean pair, still stale by now.
	feedFrame(demod, makeRepairable(odd));
	return handler.longCount == 6;
}

// A discarded repair must not modify or rejuvenate the clean position
// reference either. Proven purely on timing: several discards are attempted
// on the way to the reference's 60 s lifetime, then, once a fresh opposite
// parity is available again and only the reference's own age is left to
// decide the outcome, the reference must behave as if none of those discards
// had happened.
bool discardedRepairsDoNotRejuvenateCleanReference() {
	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);
	constexpr uint32_t icao = 0x556677;

	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 128);
	feedFrame(demod, makeIdentification(icao, 3));

	const auto even = makePosition(icao, false, 93000, 51372);
	const auto odd = makePosition(icao, true, 74158, 50194);
	feedFrame(demod, even);
	feedSilence(demod, 128);
	feedFrame(demod, odd);
	if (handler.longCount != 3)
		return false;

	// Age the opposite parity out of its 10 s pairing window first, so every
	// repair attempted below starts out discarded purely for staleness, not
	// by the accident of still catching a fresh pairing.
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 3));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 1'500'000);
	const auto afterAging = handler.longCount;

	// Still comfortably within the reference's 60 s lifetime from here.
	// Every opposite parity stays stale throughout (nothing refreshes it),
	// so every repair attempted along the way is discarded for that reason.
	// Identifying frames every 5 s keep the entry alive without ever
	// touching position state. Alternating even/odd matters here: a repair
	// that wrongly re-seeded the clean-pair tracker (rather than the
	// reference directly) would only show up once both sides of that
	// tracker were refreshed close together, so a discard stream of only
	// one parity could hide it.
	for (int round = 0; round < 9; ++round) {
		feedSilence(demod, 5'000'000);
		feedFrame(demod, makeIdentification(icao, (round % 2) ? 3 : 1));
		feedFrame(demod, makeRepairable((round % 2 == 0) ? even : odd));
	}
	if (handler.longCount != afterAging + 9)
		return false;

	// Cross the 60 s boundary (with margin): ~75.5 s since the reference was
	// set by the time the check below runs.
	for (int round = 0; round < 4; ++round) {
		feedSilence(demod, 5'000'000);
		feedFrame(demod, makeIdentification(icao, (round % 2) ? 3 : 1));
	}

	// A genuine clean frame here cannot rejuvenate the reference either --
	// its own clean counterpart (the even side) is itself long past the
	// noteCprClean's internal 10 s pairing window -- but it does refresh the
	// emitted-odd slot, isolating what is left to decide the next repair to
	// the reference's own age.
	feedFrame(demod, odd);
	const auto afterFreshOpposite = handler.longCount;

	// If any discard above had rejuvenated the reference, it would still
	// look fresh here and this would be accepted. It must not be: the
	// reference is now genuinely past its 60 s lifetime.
	feedFrame(demod, makeRepairable(even));
	return handler.longCount == afterFreshOpposite;
}

// An unbroken chain of ACCEPTED repairs must not extend the clean reference's
// lifetime either, because repairs never call the reference-setting code
// path -- only a CRC-clean odd/even pair does. Six repairs, 9 s apart, each
// riding the previous one's freshly emitted opposite parity, keep succeeding
// right up to 54 s after the original clean pair; a seventh, still with a
// fresh opposite parity, at 63 s must fail on the reference's age alone.
bool consecutiveAcceptedRepairsDoNotExtendCleanReference() {
	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);
	constexpr uint32_t icao = 0x667788;

	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 128);
	feedFrame(demod, makeIdentification(icao, 3));

	const auto even = makePosition(icao, false, 93000, 51372);
	const auto odd = makePosition(icao, true, 74158, 50194);
	feedFrame(demod, even);
	feedSilence(demod, 128);
	feedFrame(demod, odd);
	if (handler.longCount != 3)
		return false;
	auto expected = handler.longCount;

	for (int round = 0; round < 6; ++round) {
		feedSilence(demod, 9'000'000);
		feedFrame(demod, makeRepairable((round % 2 == 0) ? odd : even));
		++expected;
		if (handler.longCount != expected)
			return false;
	}

	// 63 s after the original clean pair, only 9 s after the last accepted
	// repair (so its opposite parity is still fresh): the reference itself
	// must have expired despite the unbroken chain of successes above.
	feedSilence(demod, 9'000'000);
	feedFrame(demod, makeRepairable(odd));
	return handler.longCount == expected;
}

// The capture analysis in README_ADV.md found, for one clean frame, its
// address absent from the cache and unconfirmed at that point -- a possible
// mechanism for some of the indirect message loss measured there, not a
// demonstrated cause for every such event (the analysis did not separately
// distinguish ttl expiry from a hash collision for that frame). This test
// reproduces one candidate mechanism: ICAOTable's cache-slot eviction ttl
// (TTL_not_trusted, decremented once per real second, independently of and
// shorter than the trust ttl) only ever refreshes on an ACCEPTED message. It
// is not enough to show one discard followed by long silence, though --
// that alone would not separate "a discard contributed nothing" from "the
// slot would have expired from silence regardless." The proof needs
// repeated discards, closer together than the eviction ttl, for longer than
// it, with the slot's own initial 30 s trust-candidate window (see below)
// already spent beforehand on genuine traffic so it cannot mask the result.
// This is an existing transition (ttl expiry, not a collision), reproduced
// here as a possible mechanism; no production code changes here.
bool discardedRepairsCanStarveCacheSlotAndDemoteNextCleanFrame() {
	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);
	constexpr uint32_t icao = 0x778899;

	feedFrame(demod, makeIdentification(icao, 1));
	feedSilence(demod, 128);
	const auto firstId = makeIdentification(icao, 3);
	feedFrame(demod, firstId);
	if (handler.longCount != 1 || handler.lastLong != firstId)
		return false;

	const auto even = makePosition(icao, false, 93000, 51372);
	const auto odd = makePosition(icao, true, 74158, 50194);
	feedFrame(demod, even);
	feedSilence(demod, 128);
	feedFrame(demod, odd);
	if (handler.longCount != 3)
		return false;

	// The very first identifying frame above registered this address in
	// ICAOTable's own 30 s trust-candidate window (the fallback for a second
	// sighting arriving after the first one's cache slot already expired --
	// see confirmTrustCandidate). That registration is never consumed by the
	// second identifying frame right after it, because by then the address
	// is already known through the ordinary path; it only lapses on its own
	// after 30 s. Spend that window here on genuine clean traffic -- kept
	// well inside the cache ttl throughout, so the slot itself stays alive
	// on real messages, not yet on anything under test -- so it cannot later
	// mask a discard's effect by confirming a next sighting on its own.
	// Seven rounds of 5 s clear the 30 s window with margin.
	auto afterTrustCandidateWindow = handler.longCount;
	for (int round = 0; round < 7; ++round) {
		feedSilence(demod, 5'000'000);
		feedFrame(demod, makeIdentification(icao, (round % 2) ? 3 : 1));
		++afterTrustCandidateWindow;
		if (handler.longCount != afterTrustCandidateWindow)
			return false;
	}

	// Now the actual test: repairs implausible at any freshness (far outside
	// the reference zone), so every one is discarded on the distance check
	// alone, independent of the pairing window's timing state -- attempted
	// every 3 s, closer together than the cache ttl (ten sweep passes, one
	// per real second, so on the order of ten seconds), for eighteen
	// seconds, longer than it. If discards refreshed the slot the way an
	// accepted message does, this stream of them, closer together than the
	// ttl, would keep it alive indefinitely; the point is that they do not.
	const auto farAway = makePosition(icao, false, 0, 0);
	for (int round = 0; round < 6; ++round) {
		feedSilence(demod, 3'000'000);
		feedFrame(demod, makeRepairable(farAway));
	}
	if (handler.longCount != afterTrustCandidateWindow)
		return false; // every repair above was discarded, as expected

	// The cache slot has expired from eighteen seconds of nothing but
	// discarded repairs. The next CRC-clean frame for this address is
	// withheld, exactly like a brand-new address's first sighting -- not
	// emitted as a known aircraft's next message. If the discards above had
	// kept the slot alive, this would be emitted immediately instead.
	const auto afterStarve = makeIdentification(icao, 1);
	feedFrame(demod, afterStarve);
	if (handler.longCount != afterTrustCandidateWindow)
		return false;

	// A second sighting re-admits it, indistinguishable from a genuinely new
	// address.
	feedFrame(demod, makeIdentification(icao, 3));
	return handler.longCount == afterTrustCandidateWindow + 1;
}

struct MlatEvent {
	bool isLong;
	uint64_t mlatTime;
};

// Mirrors what StdOutMessageHandler actually does in production: converts
// the raw sample index to the MLAT timestamp for the configured NumStreams
// before recording it, instead of observing the raw sample index directly.
struct ProductionCapturingHandler {
	static constexpr int NumStreams = 24;

	void handleShort(uint64_t sampleIndex, uint64_t frame) {
		events.push_back({ false,
			MLAT::sampleIndexToMlatTime<NumStreams>(sampleIndex) });
		shortFrames.push_back(frame);
		++shortCount;
	}

	void handleLong(uint64_t sampleIndex, const Bits128& frame) {
		events.push_back({ true,
			MLAT::sampleIndexToMlatTime<NumStreams>(sampleIndex) });
		longFrames.push_back(frame);
		lastLong = frame;
		++longCount;
	}

	uint32_t shortCount { 0 };
	uint32_t longCount { 0 };
	Bits128 lastLong;
	std::vector<MlatEvent> events;
	std::vector<uint64_t> shortFrames;
	std::vector<Bits128> longFrames;
};

// A significant mixed sequence at a production stream count (24, matching
// the deployed 6/10 -> 24 Msps configuration): short and long frames,
// accepted and discarded repairs, all interleaved with live short-frame
// (DF11) traffic that keeps flowing regardless of what the position gate is
// doing. Checks the actual emitted MLAT timestamps -- what a downstream
// Beast/AVR consumer sees -- stay ordered, and separately that the discarded
// frame never appears anywhere in the output. Nondecreasing MLAT order alone
// would also pass for a bounded buffer replayed strictly in order; it is the
// second check, not the first, that rules out buffering and retroactive
// emission.
bool productionMlatTimestampsStayOrderedAcrossMixedTraffic() {
	ProductionCapturingHandler handler;
	DemodCore<ProductionCapturingHandler::NumStreams, ProductionCapturingHandler> demod(handler);
	constexpr uint32_t icao = 0x99aabb;

	feedFrameLongN(demod, makeIdentification(icao, 1));
	feedSilenceN(demod, 128);
	feedFrameLongN(demod, makeIdentification(icao, 3));
	feedSilenceN(demod, 128);
	feedFrameShortN(demod, makeDF11(icao));

	const auto even = makePosition(icao, false, 93000, 51372);
	const auto odd = makePosition(icao, true, 74158, 50194);
	feedSilenceN(demod, 128);
	feedFrameLongN(demod, even);
	feedSilenceN(demod, 128);
	feedFrameShortN(demod, makeDF11(icao));
	feedSilenceN(demod, 128);
	feedFrameLongN(demod, odd);
	if (handler.longCount != 3 || handler.shortCount != 2)
		return false;

	// An accepted repair, close on the heels of live short-frame traffic.
	feedSilenceN(demod, 128);
	feedFrameShortN(demod, makeDF11(icao));
	feedSilenceN(demod, 128);
	feedFrameLongN(demod, makeRepairable(even));
	if (handler.longCount != 4 || handler.shortCount != 3)
		return false;

	// Age the pairing window out while short-frame traffic keeps flowing --
	// production surveillance does not stop because a position repair is
	// unavailable. The extra silence after each DF11 send settles this
	// harness's own broadcast-to-every-stream fixture (see feedFrameShortN)
	// before the next round starts; it is fixture bookkeeping, not part of
	// the behavior under test.
	for (int round = 0; round < 4; ++round) {
		feedSilenceN(demod, 3'000'000);
		feedFrameShortN(demod, makeDF11(icao));
		feedSilenceN(demod, 4096);
	}
	if (handler.shortCount != 7)
		return false;

	// Discarded for a stale opposite parity, mid-stream of otherwise-flowing
	// short traffic.
	const auto ghost = makePosition(icao, false, 76616, 51372);
	feedSilenceN(demod, 128);
	feedFrameLongN(demod, makeRepairable(ghost));
	feedSilenceN(demod, 4096);
	if (handler.longCount != 4)
		return false;
	feedSilenceN(demod, 128);
	feedFrameShortN(demod, makeDF11(icao));
	feedSilenceN(demod, 4096);
	if (handler.shortCount != 8)
		return false;

	// A fresh clean opposite parity, then a repair resumes.
	feedSilenceN(demod, 128);
	feedFrameLongN(demod, odd);
	feedSilenceN(demod, 4096);
	feedFrameShortN(demod, makeDF11(icao));
	feedSilenceN(demod, 4096);
	feedFrameLongN(demod, makeRepairable(even));
	feedSilenceN(demod, 4096);
	if (handler.longCount != 6 || handler.shortCount != 9)
		return false;

	for (size_t i = 1; i < handler.events.size(); ++i) {
		if (handler.events[i].mlatTime < handler.events[i - 1].mlatTime)
			return false;
	}

	for (const auto& emitted : handler.longFrames) {
		if (emitted == ghost)
			return false;
	}
	return true;
}

} // namespace

int main() {
	if (!fixturesHaveExpectedGeometry())
		return 1;

	if (!repairedPairCannotBypassGlobalGate(true))
		return 14;
	if (!repairedPairCannotBypassGlobalGate(false))
		return 15;

	if (!discardedRepairDoesNotRefreshEmittedHistory())
		return 19;
	if (!discardedRepairsDoNotRejuvenateCleanReference())
		return 20;
	if (!consecutiveAcceptedRepairsDoNotExtendCleanReference())
		return 21;
	if (!discardedRepairsCanStarveCacheSlotAndDemoteNextCleanFrame())
		return 22;
	if (!productionMlatTimestampsStayOrderedAcrossMixedTraffic())
		return 23;

	CapturingHandler handler;
	DemodCore<1, CapturingHandler> demod(handler);

	// 0x123456 becomes trusted on the second sighting of a clean frame; two
	// identifying frames do that without ever carrying a position.
	feedFrame(demod, makeIdentification(0x123456, 1));
	feedSilence(demod, 128);
	const auto firstIdentification = makeIdentification(0x123456, 3);
	feedFrame(demod, firstIdentification);
	if (handler.longCount != 1 || handler.lastLong != firstIdentification)
		return 2;

	// A repaired airborne position for a trusted aircraft with no clean
	// odd/even pair behind it must be rejected: the gate stays closed when
	// nothing is known, repairs never establish the first position.
	feedSilence(demod, 128);
	feedFrame(demod, makeRepairable(makePosition(0x123456, false, 93000, 51372)));
	if (handler.longCount != 1 || handler.lastLong != firstIdentification)
		return 3;

	// A CRC-clean even/odd pair establishes the reference position; both
	// frames are emitted.
	const auto firstEven = makePosition(0x123456, false, 93000, 51372);
	feedFrame(demod, firstEven);
	feedSilence(demod, 128);
	const auto firstOdd = makePosition(0x123456, true, 74158, 50194);
	feedFrame(demod, firstOdd);
	if (handler.longCount != 3 || handler.lastLong != firstOdd)
		return 4;

	// A single-bit damage repair landing on the established position passes
	// the global pair check: the repair restores the clean even frame, which
	// pairs with the clean odd to the reference position.
	feedSilence(demod, 128);
	feedFrame(demod, makeRepairable(firstEven));
	if (handler.longCount != 4 || handler.lastLong != firstEven)
		return 5;

	// The ghost counterexample: a damaged even frame whose repaired CPR
	// decodes locally 84 km from the reference, inside the gate, but whose
	// global decode against the clean odd lands 4700 km away in another
	// latitude zone. The raw CPR bits go out on the wire, so the pair a
	// receiver would actually form is what the gate must validate.
	const auto ghost = makePosition(0x123456, false, 76616, 51372);
	feedSilence(demod, 128);
	feedFrame(demod, makeRepairable(ghost));
	if (handler.longCount != 4 || handler.lastLong != firstEven)
		return 6;

	// A rejected even repair must not enter the emitted-CPR cache. If it did,
	// this valid repaired odd frame would pair with the rejected ghost and fail.
	feedSilence(demod, 128);
	feedFrame(demod, makeRepairable(firstOdd));
	if (handler.longCount != 5 || handler.lastLong != firstOdd)
		return 7;

	// A repair that would place the aircraft far outside the reference zone
	// is rejected as well.
	const auto farAway = makePosition(0x123456, false, 0, 0);
	feedSilence(demod, 128);
	feedFrame(demod, makeRepairable(farAway));
	if (handler.longCount != 5 || handler.lastLong != firstOdd)
		return 8;

	// --- a southern and western reference ---

	// 0x234567 becomes trusted like above.
	feedFrame(demod, makeIdentification(0x234567, 1));
	feedSilence(demod, 128);
	const auto southIdentification = makeIdentification(0x234567, 3);
	feedFrame(demod, southIdentification);
	if (handler.longCount != 6 || handler.lastLong != southIdentification)
		return 9;

	// A clean pair near Santiago (33.55 S, 70.80 W) seeds a negative
	// reference; both frames are emitted.
	const auto southEven = makePosition(0x234567, false, 53521, 47623);
	feedFrame(demod, southEven);
	feedSilence(demod, 128);
	const auto southOdd = makePosition(0x234567, true, 65736, 73400);
	feedFrame(demod, southOdd);
	if (handler.longCount != 8 || handler.lastLong != southOdd)
		return 10;

	// Age the odd parity out of the 10 s pair window while the reference
	// stays young and the entry alive: identifying frames keep the address
	// trusted, and they carry no position, so they refresh nothing else.
	// The ME type alternates because back to back identical frames are
	// dropped as duplicates before any of this runs.
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(0x234567, 1));
	feedSilence(demod, 3'000'000);
	feedFrame(demod, makeIdentification(0x234567, 3));
	feedSilence(demod, 3'000'000);
	const auto latestIdentification = makeIdentification(0x234567, 1);
	feedFrame(demod, latestIdentification);
	feedSilence(demod, 1'500'000);
	if (handler.longCount != 11 || handler.lastLong != latestIdentification)
		return 11;

	// Even a repair at the reference must wait for a fresh opposite parity:
	// discarded, not queued. (southEven's bits already went out once, as the
	// original clean frame, so it is longCount staying put that proves this
	// particular repair produced nothing new.)
	feedFrame(demod, makeRepairable(southEven));
	if (handler.longCount != 11 || handler.lastLong != latestIdentification)
		return 12;

	// The next clean opposite parity is emitted normally -- the discard above
	// does not delay or suppress it, and it is what goes out, not a
	// retroactive replay of the repair withheld just before it.
	feedFrame(demod, southOdd);
	if (handler.longCount != 12 || handler.lastLong != southOdd)
		return 16;

	// A discarded repair also leaves the clean reference and the emitted-CPR
	// history exactly as they were: this repair succeeds only because both
	// still hold the original clean pair, undisturbed by the discard above.
	feedFrame(demod, makeRepairable(southEven));
	if (handler.longCount != 13 || handler.lastLong != southEven)
		return 17;

	const auto southFar = makePosition(0x234567, false, 0, 0);
	feedFrame(demod, makeRepairable(southFar));
	if (handler.longCount != 13 || handler.lastLong != southEven
			|| !frameWasNeverEmitted(handler, southFar))
		return 13;

	// The whole sequence above went out with strictly increasing timestamps:
	// no buffering, no reordering, no retroactive emission anywhere in it.
	if (!outputTimestampsAreMonotonic(handler))
		return 18;

	return 0;
}
