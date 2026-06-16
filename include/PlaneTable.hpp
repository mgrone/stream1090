#pragma once

#include <string>
#include <iostream>
#include <unordered_map>
#include <memory>
#include "ModeS.hpp"

typedef uint32_t icao_t;

struct PlaneData {
    uint32_t altitude;
    uint32_t squawk;
};

struct IcaoHexHash {
    std::size_t operator()(const uint32_t& hex) const noexcept {
        return hex;
    }
};

class PlaneTable {
public:
    using MapType = std::unordered_map<uint32_t, PlaneData, IcaoHexHash>;

    auto upsert(uint32_t icao) {
        auto it = m_table.find(icao);
        if (it == m_table.end()) {
            return m_table.insert({icao, PlaneData()}).first;
        } else {
            return it;
        }
    }

    void upsertAlt(uint32_t icao, uint32_t altitude) {
        const auto it = upsert(icao);
        (*it).second.altitude = altitude;
    }

    void upsertSquawk(uint32_t icao, uint32_t squawk) {
        const auto it = upsert(icao);
        (*it).second.squawk = squawk;
    }

    void print() {
        std::cerr << "\x1B[2J\x1B[H" << std::endl;
        for (const auto& [key, plane] : m_table) {
            std::cerr << std::hex << std::setfill('0') << std::setw(6) << key << std::dec << " alt " << plane.altitude << " squawk " << ModeS::decodeSquawk(plane.squawk) << std::endl;
        }
    }

    void tick() {
        if (m_timeCnt == 0) {
            m_timeCnt = 5000000;
            print();    
            m_table.clear();
        } else {
            m_timeCnt--;
        }
    }
protected:
    uint32_t m_timeCnt = 5000000;    
    MapType m_table;
};
