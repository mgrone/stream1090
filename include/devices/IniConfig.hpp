/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Martin Gronemann
 *
 * This file is part of stream1090 and is licensed under the GNU General
 * Public License v3.0. See the top-level LICENSE file for details.
 */
#pragma once
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <algorithm>

class IniConfig {
public:
    using Section = std::map<std::string, std::string>;
    using Data = std::map<std::string, Section>;

    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open())
            return false;

        std::string line;
        std::string currentSection;

        while (std::getline(file, line)) {
            trim(line);

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            // Section header
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                trim(currentSection);
                data[currentSection] = Section();
                continue;
            }

            // Key-value pair
            auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;

            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            trim(key);
            trim(value);

            data[currentSection][key] = value;
        }

        return true;
    }

    const Data& get() const { return data; }

private:
    Data data;

    static void trim(std::string& s) {
        auto notSpace = [](int ch) { return !std::isspace(ch); };

        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    }
};
