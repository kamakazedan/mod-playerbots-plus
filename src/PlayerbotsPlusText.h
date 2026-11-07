#ifndef PLAYERBOTS_PLUS_TEXT_H
#define PLAYERBOTS_PLUS_TEXT_H

// Text helpers with no game types in them, so they can be tested without a
// server (tests/text_test.cpp).

#include <cstdint>
#include <string>
#include <vector>

// `pattern` against the whole of `text`, ignoring case; '*' matches any run
// of characters, so "Using |c*" is "Using " followed by an item link, and
// "Staying" is that word alone.
bool PbpGlobMatch(std::string const& pattern, std::string const& text);

// `raw` cut at `sep`, each part trimmed, empty ones left out.
std::vector<std::string> PbpSplit(std::string const& raw, char sep);

// Playerbots' red error line to a master, "Name, Name|cfff00000: text"
// (PlayerbotMgr::CheckTellErrors): the names, and the text. False for any
// other line.
bool PbpSplitErrorLine(std::string const& line, std::vector<std::string>& names, std::string& text);

// A place on a map, as a config line: "map x y z radius capacity name", the
// name being the rest of the line ("0 -8833.4 628.6 94 22 14 the Trade
// District in Stormwind"). The radius is kept to 3..60 yards and the capacity
// to 200. False when the numbers are not all there.
struct PbpSpotLine
{
    uint32_t    map      = 0;
    float       x        = 0.0f;
    float       y        = 0.0f;
    float       z        = 0.0f;
    float       radius   = 0.0f;
    uint32_t    capacity = 0;
    std::string name;   // empty when the line has none
};
bool PbpParseSpotLine(std::string const& text, PbpSpotLine& out);

#endif // PLAYERBOTS_PLUS_TEXT_H
