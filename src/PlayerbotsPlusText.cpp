#include "PlayerbotsPlusText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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
