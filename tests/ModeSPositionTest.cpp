/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "ModeS.hpp"

#include <cmath>

int main() {
	// Publicly documented airborne CPR example: the even/odd pair decodes
	// near 52.2572 N, 3.91937 E.
	double lat = 0.0;
	double lon = 0.0;
	if (!ModeS::decodeCprGlobal(93000, 51372, 74158, 50194, lat, lon))
		return 1;

	return !(std::abs(lat - 52.2572) < 0.001
		&& std::abs(lon - 3.91937) < 0.001);
}