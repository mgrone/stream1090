# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2025 Martin Gronemann
#
# This file is part of stream1090 and is licensed under the GNU General
# Public License v3.0. See the top-level LICENSE file for details.
#


from scipy.signal import firwin

numtaps = 15

fs = 10_000_000 
cutoff_hz = 2_000_000 
h = firwin(numtaps, cutoff_hz, fs=fs, width=0.02)

for x in h:
    print(x)