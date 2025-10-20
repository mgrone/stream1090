/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#include <iostream>
#include <vector>
#include "CRC.hpp"
#include "Sampler.hpp"

using namespace CRC;

int hashFunction(crc_t crc, int N) {
    return (crc % N);
}

void generateKeySetExtSquitter(std::vector<crc_t>& keys) {
    // skip the first 5 bits (df), first do all one bit corrections
    for (int i = 0; i < 112-5; i++) {
        keys.push_back(compute(encodeFixOp(0x1,i)));
    }

    // now two bit bursts
    for (int i = 0; i < 111-5; i++) {
        keys.push_back(compute(encodeFixOp(0x3,i)));
    }

    // this seems to help, for the parity block
    for (int i = 0; i < 16; i++) {
        keys.push_back(compute(encodeFixOp(129,i)));
    }
}

void generateKeySetExtSquitterBurst(std::vector<crc_t>& keys) {
    // skip the first 5 bits (df), first do all one bit corrections
    for (int i = 0; i < 112-5; i++) {
        keys.push_back(compute(encodeFixOp(0x1,i)));
    }

    // now two bit bursts (11)
    for (int i = 0; i < 111-5; i++) {
        keys.push_back(compute(encodeFixOp(0x3,i)));
    }

    // now three bit bursts (111)
    for (int i = 0; i < 110-5; i++) {
        keys.push_back(compute(encodeFixOp(0x7,i)));
    }

    // this seems to help, for the parity block
    for (int i = 0; i < 16; i++) {
        keys.push_back(compute(encodeFixOp(129,i)));
    }
}

void generateKeySetShort1Bit(std::vector<crc_t>& keys) {
    for (int i = 0; i < 56-5; i++) {
        keys.push_back(compute(encodeFixOp(0x1,i)));
    }
}

void generateKeySetShort2Bit(std::vector<crc_t>& keys) {
    for (int i = 0; i < 56-5; i++) {
        keys.push_back(compute(encodeFixOp(0x1,i)));
    }

    for (int i = 0; i < 55-5; i++) {
        keys.push_back(compute(encodeFixOp(0x3,i)));
    }
}

int testTableSize(const std::vector<crc_t>& keys, int tableSize) {
    std::vector<int> collisions(tableSize, 0);
    int maxCollisions = 0; 
    for (auto k : keys) {
        auto index = hashFunction(k, tableSize);
        collisions[index]++;
        maxCollisions = std::max(collisions[index], maxCollisions); 
    }
    return maxCollisions;
}

int bruteforceMinTableSize(const std::vector<crc_t>& keys) {
    for (int N = keys.size(); N < 6000; N++) {
        int res = testTableSize(keys, N);
        if (res == 1) {
            return N;
        }
    }
    return 0;
}

int runExtSquitter() {
    std::vector<crc_t> keys;
    generateKeySetExtSquitter(keys);
    return bruteforceMinTableSize(keys);
}

int runExtSquitterBurst() {
    std::vector<crc_t> keys;
    generateKeySetExtSquitterBurst(keys);
    return bruteforceMinTableSize(keys);
}

int runOneBitShort() {
    std::vector<crc_t> keys;
    generateKeySetShort1Bit(keys);
    return bruteforceMinTableSize(keys);
}

int runTwoBitShort() {
    std::vector<crc_t> keys;
    generateKeySetShort2Bit(keys);
    return bruteforceMinTableSize(keys);
}

int main(/*int argc, char** argv*/) {
    std::cout << "DF17 min table size: " << runExtSquitter() << std::endl;
    std::cout << "DF17 min table size with advanced correction: " << runExtSquitterBurst() << std::endl;
    std::cout << "DF11 one bit short message min table size: " << runOneBitShort() << std::endl;
    std::cout << "DF11 one bit short message min table size: " << runTwoBitShort() << std::endl;
    return 0;
}
