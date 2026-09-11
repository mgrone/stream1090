/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ModeS.hpp"

#include <cmath>

int main() {
	// Publicly documented airborne CPR example: the even/odd pair decodes
	// near 52.2572 N, 3.91937 E.
	double lat = 0.0;
	double lon = 0.0;
	if (!ModeS::decodeCprGlobal(93000, 51372, 74158, 50194, false, lat, lon))
		return 1;

	if (!(std::abs(lat - 52.2572) < 0.001
			&& std::abs(lon - 3.91937) < 0.001))
		return 1;

	// The odd solution is the fix for the odd frame: 52.2658 N, 3.93891 E.
	// The two solutions differ by the distance flown between the frames, so
	// a receiver holding a newer odd frame must report this one.
	if (!ModeS::decodeCprGlobal(93000, 51372, 74158, 50194, true, lat, lon))
		return 2;

	if (!(std::abs(lat - 52.2658) < 0.001
			&& std::abs(lon - 3.93891) < 0.001))
		return 2;

	// This pair resolves consistently in the same NL zone, but at 103.7
	// degrees latitude. Airborne global CPR must reject the impossible fix.
	if (ModeS::decodeCprGlobal(37776, 0, 0, 0, false, lat, lon))
		return 3;

	// Both latitudes are individually valid, but they straddle an NL boundary
	// and therefore cannot form one globally unambiguous airborne fix.
	if (ModeS::decodeCprGlobal(75765, 0, 106804, 0, false, lat, lon))
		return 4;

	return 0;
}
