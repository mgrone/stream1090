/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2025 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */

#pragma once

#include <array>
#include <iostream>
#include "ModeS.hpp"
#include <math.h>

namespace Stats {

    enum EventType {
        NUM_STREAMS = 0,
        
        //SAMPLES_PROCESSED,    // sample has been processed
        NUM_ITERATIONS,
    
        //SKIP_LONG_EQUAL,      // handle long msg has been skipped because the previous stream 
        //SKIP_SHORT_EQUAL,     // handle short msg has been skipped because the previous stream
        
        // this section refers to extended squitter, thus includes also DF18,19 
        DF17_HEADER,          // DF 17, 18, or 19 detected
        DF17_GOOD_MESSAGE,    // the message is good. Crc is fine and the sender is known to DF11
        DF17_BAD_MESSAGE,     // 
        DF17_REPAIR_SUCCESS,
        DF17_REPAIR_FAILED,

        //COMM_B_HEADER,
        COMM_B_GOOD_MESSAGE,

        //ACAS_SURV_HEADER,
        ACAS_SURV_GOOD_MESSAGE,

        //DF11_HEADER,
        DF11_ICAO_CA_FOUND,
        DF11_ICAO_CA_FOUND_GOOD_CRC,
        DF11_ICAO_CA_FOUND_1_BIT_FIX,
        DF11_ICAO_CA_FOUND_BAD_CRC,
        DF11_NEW_GOOD_CRC,

        NUM_EVENTS
    };



class StatsLog {
    public:

    constexpr StatsLog() : m_events(), m_sent(), m_dups() {
        reset();
    }

    constexpr void reset() {
        for (auto i = 0; i < Stats::NUM_EVENTS; i++) {
            m_events[i] = 0;
        }

        for (auto i = 0; i < 25; i++) {
            m_sent[i] = 0;
            m_dups[i] = 0;
        }
    }

    constexpr double elapsedTime() const {
        return (double)getCount(NUM_ITERATIONS) / 1000000.0;
    }

    constexpr void updateGlobalStats() {
        auto totalSent = 0;
        for (auto i = 0; i < 25; i++) {
            totalSent += m_sent[i];
        }
        const double msgsPerSec = (double)totalSent/elapsedTime();
        if (msgsPerSec > m_maxMsgsPerSecond)
            m_maxMsgsPerSecond = msgsPerSec;
        m_totalMsgsSent += totalSent;
    }

    constexpr void log(EventType evt, int count = 1) {
        m_events[evt] += count;
    }

    constexpr void logSent(int df) {
        m_sent[df]++;
    }

    constexpr void logDup(int df) {
        m_dups[df]++;
    }

    constexpr uint64_t getCount(EventType evt) const {
        return m_events[evt];
    }

    constexpr int getSent(int df) const {
        return m_sent[df];
    }

    constexpr int getDups(int df) const {
        return m_dups[df];
    }

    constexpr double maxMsgsPerSec() const {
        return m_maxMsgsPerSecond;
    }

    constexpr int totalMessagesSent() const {
        return m_totalMsgsSent;
    }
private:
    std::array<uint64_t, Stats::NUM_EVENTS> m_events;
    std::array<int, 25> m_sent;
    std::array<int, 25> m_dups;
    double m_maxMsgsPerSecond = 0.0;
    int m_totalMsgsSent = 0;
};

    inline void printLabel(std::ostream& out, const std::string& label, int width) {
        out << std::setfill(' ') << std::setw(width) << label;
    }

    inline void printNumber(std::ostream& out, int n, int width) {
        out << std::setw(width) << n;
    }

    inline void printPerc(std::ostream& out, double p, int width) {
        if (p >= 0.0) {
            double perc = (double)std::round(p * 1000.0) / 10.0;
            out << std::setfill(' ') << std::setw(width) << std::setprecision(4) << perc << "%";
        } else { 
            out << std::setw(width+1) << "     ";
        }
    }

    inline void printDouble(std::ostream& out, double d, int width) {
        out << std::setfill(' ') << std::setw(width) << std::setprecision(4) << d;
    }

    inline void printSep(std::ostream& out) {
        out << " | ";
    }

    inline void printLeftSep(std::ostream& out) {
        out << "|";
    }

    inline void printLine(std::ostream& out) {
        out << "-------------------------------------------------------------" << std::endl;
    }

    inline void printStatsLine(std::ostream& out,
                                const std::string& label,
                                int sent,
                                int dups,
                                int repaired, 
                                int total_sent,
                                double time_elapsed) {
        const double msgs = (double)sent / time_elapsed;
        const double perc_of_total = (double)sent / (double)total_sent;
        const double perc_dups = (double)dups / (double)(sent + dups);
        const double perc_repaired = (double)repaired / (double)(sent + dups);
        printLeftSep(out);
        printLabel(out, label,9);
        printSep(out);
        printNumber(out, sent, 6);
        printSep(out);
        printPerc(out, perc_of_total, 6);
        printSep(out);
        printPerc(out, perc_dups, 6);
        printSep(out);
        printPerc(out, perc_repaired, 6);
        printSep(out);
        printDouble(out, msgs, 7);
        printSep(out);
        out << std::endl;
    }

    inline void printHeaderLine(std::ostream& out) {
        printLeftSep(out);
        printLabel(out, "Type",9);
        printSep(out);
        printLabel(out, "#Msgs", 6);
        printSep(out);
        printLabel(out, "%Total", 7);
        printSep(out);
        printLabel(out, "Dups", 7);
        printSep(out);
        printLabel(out, "Fixed", 7);
        printSep(out);
        printLabel(out, "Msg/s", 7);
        printSep(out);
        out << std::endl;
    }
    
    inline void printStats(const StatsLog& s, std::ostream& out) {
        const double time_elapsed = (double)s.getCount(NUM_ITERATIONS) / 1000000.0;
        
        const auto DF11_sent = s.getSent(11);
        const auto DF11_dups = s.getDups(11);
        const auto DF11_repaired = s.getCount(DF11_ICAO_CA_FOUND_1_BIT_FIX);

        const auto Surv_sent = s.getSent(4) + s.getSent(5);
        const auto Surv_dups = s.getDups(4) + s.getDups(5);

        const auto ACAS_sent = s.getSent(0) + s.getSent(16);
        const auto ACAS_dups = s.getDups(0) + s.getDups(16);

        const auto COMM_B_sent = s.getSent(20) + s.getSent(21);
        const auto COMM_B_dups = s.getDups(20) + s.getDups(21);
         
        const auto ES_sent = s.getSent(17) + s.getSent(18) + s.getSent(19);
        const auto ES_dups = s.getDups(17) + s.getDups(18) + s.getDups(19);
        const auto ES_repaired = s.getCount(DF17_REPAIR_SUCCESS);
        
        const auto Short_sent = DF11_sent + Surv_sent + s.getSent(0);
        const auto Short_dups = DF11_dups + Surv_dups + s.getDups(0);
        const auto Short_repaired = DF11_repaired;

        const auto Long_sent = s.getSent(16) + COMM_B_sent + ES_sent;
        const auto Long_dups = s.getDups(16) + COMM_B_dups + ES_dups;
        const auto Long_repaired = ES_repaired;

        const auto total_sent = Long_sent + Short_sent;
        const auto total_dups = Long_dups + Short_dups;
        const auto total_repaired = Long_repaired + Short_repaired;
#if !(defined(STATS_END_ONLY) && STATS_END_ONLY)
        out << "\x1B[2J\x1B[H";
#endif
        printLine(out);
        printHeaderLine(out);
        printLine(out);
        printStatsLine(out, "ADS-B", ES_sent, ES_dups, ES_repaired, total_sent, time_elapsed);
        printStatsLine(out, "Comm-B", COMM_B_sent, COMM_B_dups, -1, total_sent, time_elapsed);
        printStatsLine(out, "ACAS", ACAS_sent, ACAS_dups, -1, total_sent, time_elapsed);
        printStatsLine(out, "Surv", Surv_sent, Surv_dups, -1, total_sent, time_elapsed);
        printStatsLine(out, "DF-11", DF11_sent, DF11_dups, DF11_repaired, total_sent, time_elapsed);
        printLine(out);
        printStatsLine(out, "112-bit", Long_sent, Long_dups, Long_repaired, total_sent, time_elapsed);
        printStatsLine(out, "56-bit", Short_sent, Short_dups, Short_repaired, total_sent, time_elapsed);
        printLine(out);
        printStatsLine(out, "Total", total_sent, total_dups, total_repaired, total_sent, time_elapsed);
        printLine(out);
        out << "(Max. msgs/s "<< s.maxMsgsPerSec() << ")" << std::endl;
        out << "Messages Total "<< s.totalMessagesSent() << std::endl;
        for (int i = 0; i < 24; i++) {
            if (s.getSent(i) > 0) {
                out << "DF " << i << " : "<< s.getSent(i) << std::endl;
            }
        }
        out << s.getCount(NUM_ITERATIONS) << " iterations @1MHz" << std::endl; 
    }

    inline void printTick(StatsLog& stats, std::ostream& out) {
        if (stats.getCount(NUM_ITERATIONS) >= 5000000) {
            stats.updateGlobalStats();
            printStats(stats, out);
            stats.reset();
        }
    }
    
    inline void printStatsOnExit(StatsLog& stats, std::ostream& out) {
            stats.updateGlobalStats();
            printStats(stats, out);
    }
}
