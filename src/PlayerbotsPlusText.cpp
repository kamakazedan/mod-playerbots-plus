#include "PlayerbotsPlusText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <sstream>

bool PbpGlobMatch(std::string const& pattern, std::string const& text)
{
    if (pattern.empty())
        return false;
    // Iterative, with one backtrack point for the last '*'.
    size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    auto same = [](char a, char b)
    {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    while (t < text.size())
    {
        if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            mark = t;
        }
        else if (p < pattern.size() && same(pattern[p], text[t]))
        {
            ++p;
            ++t;
        }
        else if (star != std::string::npos)
        {
            p = star + 1;
            t = ++mark;
        }
        else
            return false;
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

std::vector<std::string> PbpSplit(std::string const& raw, char sep)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= raw.size())
    {
        size_t end = raw.find(sep, start);
        if (end == std::string::npos)
            end = raw.size();
        size_t b = start, e = end;
        while (b < e && std::isspace(static_cast<unsigned char>(raw[b])))
            ++b;
        while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1])))
            --e;
        if (e > b)
            out.push_back(raw.substr(b, e - b));
        start = end + 1;
    }
    return out;
}

bool PbpSplitErrorLine(std::string const& line, std::vector<std::string>& names, std::string& text)
{
    static const std::string kMark = "|cfff00000: ";
    const size_t mark = line.find(kMark);
    if (mark == std::string::npos || mark == 0)
        return false;
    names.clear();
    size_t start = 0;
    while (start < mark)
    {
        size_t end = line.find(", ", start);
        if (end == std::string::npos || end > mark)
            end = mark;
        if (end == start)
            return false;
        names.push_back(line.substr(start, end - start));
        start = end + 2;
    }
    text = line.substr(mark + kMark.size());
    return true;
}

namespace
{
    // In this order: PlayerbotsPlusPets.cpp matches the game's holidays to it.
    char const* const kHolidayKeys[] = { "winterveil",   "hallowsend", "midsummer",      "loveisintheair",
                                         "noblegarden",  "childrensweek", "brewfest",    "pilgrimsbounty",
                                         "lunarfestival", "darkmoon" };
}

uint8_t PbpHolidayKey(std::string const& key)
{
    std::string lower = key;
    for (char& c : lower)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    for (size_t i = 0; i < std::size(kHolidayKeys); ++i)
        if (lower == kHolidayKeys[i])
            return uint8_t(i + 1);
    return 0;
}

std::vector<std::string> PbpHolidayKeys()
{
    return std::vector<std::string>(std::begin(kHolidayKeys), std::end(kHolidayKeys));
}

bool PbpParseSpotLine(std::string const& text, PbpSpotLine& out)
{
    std::istringstream in(text);
    PbpSpotLine line;
    long long map = -1, capacity = -1;
    if (!(in >> map >> line.x >> line.y >> line.z >> line.radius >> capacity) || map < 0 || capacity < 0 ||
        !std::isfinite(line.x) || !std::isfinite(line.y) || !std::isfinite(line.z) || !std::isfinite(line.radius))
        return false;
    line.map      = static_cast<uint32_t>(map);
    line.capacity = static_cast<uint32_t>(std::min<long long>(capacity, 200));
    line.radius   = std::clamp(line.radius, 3.0f, 60.0f);
    std::string rest;
    std::getline(in, rest);
    const size_t b = rest.find_first_not_of(" \t\r\n");
    if (b != std::string::npos)
        line.name = rest.substr(b, rest.find_last_not_of(" \t\r\n") - b + 1);
    out = std::move(line);
    return true;
}

std::string PbpCityOf(std::string const& spotName)
{
    std::string city = spotName;
    for (std::string const& mark : { std::string(" in "), std::string(" of "), std::string("outside ") })
    {
        const size_t at = spotName.rfind(mark);
        if (at != std::string::npos && at + mark.size() < spotName.size())
        {
            city = spotName.substr(at + mark.size());
            break;
        }
    }
    if (city.size() > 4 && (city.rfind("the ", 0) == 0 || city.rfind("The ", 0) == 0))
        city = city.substr(4);
    return city;
}
