#ifndef PLAYERBOTS_PLUS_TEXT_H
#define PLAYERBOTS_PLUS_TEXT_H

// Text helpers with no game types in them, so they can be tested without a
// server (tests/text_test.cpp).

#include <cstdint>
#include <map>
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

// The city a hangout spot is in, from its name: what follows its last " in ",
// else its last " of ", else "outside ", without a "the " in front ("outside
// the bank in Thunder Bluff" -> "Thunder Bluff", "the main hall of the
// Exodar" -> "Exodar", "the zeppelin tower outside Orgrimmar" ->
// "Orgrimmar"). The whole name when none of those is in it.
std::string PbpCityOf(std::string const& spotName);

// A hangout spot, as the places are shared out among them.
struct PbpSpotShare
{
    std::string city;           // PbpCityOf its name
    uint32_t    capacity = 0;   // as written: its part against the others of its kind in its city
    bool        hot      = false;
};

// How many people each spot has room for when `places` are shared out among
// the cities by `weights` (by city; 0: none there), and within a city
// `hotShare` percent of its places among its hot spots and the rest among the
// others -- all among one kind when it has only that kind -- each by its
// written capacity (by turns when those are all 0). A city not in `weights`
// keeps its spots' written capacities and takes no part. `places` 0: what the
// written capacities of the weighted cities' spots add up to. Whole places,
// rounded to add up (the largest remainders get the odd ones).
std::vector<uint32_t> PbpShareHangoutPlaces(std::vector<PbpSpotShare> const& spots,
                                            std::map<std::string, uint32_t> const& weights, uint32_t places,
                                            uint32_t hotShare);

// The holidays a companion can belong to, as the conf names them
// ("winterveil", "brewfest"): 1 and up for one of them, in a fixed order the
// game's holiday ids are matched to (PlayerbotsPlusPets.cpp); 0 for anything
// else. Case is ignored.
uint8_t PbpHolidayKey(std::string const& key);
std::vector<std::string> PbpHolidayKeys();

#endif // PLAYERBOTS_PLUS_TEXT_H
