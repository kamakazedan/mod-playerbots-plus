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

namespace
{
    // `total` shared out by `weights` in whole parts that add up to it; all
    // weights 0, nothing for any.
    std::vector<uint32_t> Apportion(uint32_t total, std::vector<uint64_t> const& weights)
    {
        std::vector<uint32_t> out(weights.size(), 0);
        uint64_t sum = 0;
        for (uint64_t w : weights)
            sum += w;
        if (!sum || !total)
            return out;
        std::vector<std::pair<uint64_t, size_t>> rest;   // remainder, which
        uint64_t given = 0;
        for (size_t i = 0; i < weights.size(); ++i)
        {
            const uint64_t share = uint64_t(total) * weights[i];
            out[i] = uint32_t(share / sum);
            given += out[i];
            rest.emplace_back(share % sum, i);
        }
        std::stable_sort(rest.begin(), rest.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
        for (size_t k = 0; given < total && k < rest.size(); ++k)
            if (weights[rest[k].second])
            {
                ++out[rest[k].second];
                ++given;
            }
        return out;
    }
}

std::vector<uint32_t> PbpShareHangoutPlaces(std::vector<PbpSpotShare> const& spots,
                                            std::map<std::string, uint32_t> const& weights, uint32_t places,
                                            uint32_t hotShare)
{
    std::vector<uint32_t> out;
    for (PbpSpotShare const& s : spots)
        out.push_back(s.capacity);

    // The weighted cities, in the order their first spot comes.
    std::vector<std::string> cities;
    std::map<std::string, std::vector<size_t>> members;
    uint64_t written = 0;
    for (size_t i = 0; i < spots.size(); ++i)
    {
        if (!weights.count(spots[i].city))
            continue;
        if (!members.count(spots[i].city))
            cities.push_back(spots[i].city);
        members[spots[i].city].push_back(i);
        written += spots[i].capacity;
    }
    std::vector<uint64_t> cityWeights;
    for (std::string const& city : cities)
        cityWeights.push_back(weights.at(city));
    const std::vector<uint32_t> cityPlaces =
        Apportion(places ? places : uint32_t(std::min<uint64_t>(written, UINT32_MAX)), cityWeights);

    auto shareAmong = [&](std::vector<size_t> const& group, uint32_t n)
    {
        std::vector<uint64_t> w;
        bool any = false;
        for (size_t i : group)
        {
            w.push_back(spots[i].capacity);
            any = any || spots[i].capacity;
        }
        if (!any)
            std::fill(w.begin(), w.end(), 1);
        const std::vector<uint32_t> parts = Apportion(n, w);
        for (size_t k = 0; k < group.size(); ++k)
            out[group[k]] = parts[k];
    };
    for (size_t c = 0; c < cities.size(); ++c)
    {
        std::vector<size_t> hot, others;
        for (size_t i : members[cities[c]])
            (spots[i].hot ? hot : others).push_back(i);
        uint32_t hotPlaces = hot.empty() ? 0 : cityPlaces[c];
        if (!hot.empty() && !others.empty())
            hotPlaces = uint32_t((uint64_t(cityPlaces[c]) * std::min<uint32_t>(hotShare, 100) + 50) / 100);
        shareAmong(hot, hotPlaces);
        shareAmong(others, cityPlaces[c] - hotPlaces);
    }
    return out;
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
