// Tests for the text helpers (src/PlayerbotsPlusText.cpp), which have no game
// types in them, and for what the conf ships: every hangout spot reads, the
// mute patterns mute what they should and nothing else, and the defaults in
// the code are the ones the conf says. Not part of the server build:
// AzerothCore only compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusText.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void True(std::string const& what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what.c_str());
        }
    }

    void EqS(std::string const& what, std::string const& got, std::string const& want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got '%s', want '%s'\n", what.c_str(), got.c_str(), want.c_str());
        }
    }

    std::string ReadFile(std::string const& path)
    {
        std::ifstream in(path);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    // The quoted value of the first line that starts with `key = "`.
    std::string ConfValue(std::string const& conf, std::string const& key)
    {
        std::istringstream in(conf);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.rfind(key + " = \"", 0) != 0)
                continue;
            const size_t q1 = line.find('"'), q2 = line.rfind('"');
            return q2 > q1 ? line.substr(q1 + 1, q2 - q1 - 1) : std::string();
        }
        return {};
    }

    // The string literals of `name = "..." "...";` in a source file, joined.
    // The statement ends at a ';' that ends a line: the patterns have ';' in.
    std::string SourceLiteral(std::string const& source, std::string const& name)
    {
        const size_t at = source.find(name + " =");
        if (at == std::string::npos)
            return {};
        const size_t end = source.find(";\n", at);
        std::string out;
        const std::regex literal("\"((?:[^\"\\\\]|\\\\.)*)\"");
        std::string const part = source.substr(at, end - at);
        for (std::sregex_iterator it(part.begin(), part.end(), literal), stop; it != stop; ++it)
            out += (*it)[1].str();
        return out;
    }

    bool Muted(std::vector<std::string> const& patterns, std::string const& line)
    {
        for (std::string const& p : patterns)
            if (PbpGlobMatch(p, line))
                return true;
        return false;
    }
}

int main(int argc, char** argv)
{
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string conf = ReadFile(root + "/conf/mod_playerbots_plus.conf.dist");
    const std::string config = ReadFile(root + "/src/PlayerbotsPlusConfig.cpp");
    True("the conf is there", !conf.empty());
    True("the config source is there", !config.empty());

    // ---- glob ----------------------------------------------------------------------------
    True("glob exact", PbpGlobMatch("Staying", "Staying"));
    True("glob ignores case", PbpGlobMatch("staying", "STAYING"));
    True("glob whole line", !PbpGlobMatch("Staying", "Staying here till you are back"));
    True("glob star at the end", PbpGlobMatch("Using |c*", "Using |cffffffff|Hitem:6948::::::::5|h[Hearthstone]|h|r"));
    True("glob star needs its prefix", !PbpGlobMatch("Using |c*", "Using it now"));
    True("glob star in the middle", PbpGlobMatch("a*c", "abbbc"));
    True("glob stars", PbpGlobMatch("* */* |c*|Hquest:*", "Boar 3/10 |cFFFFFF00|Hquest:7:5|h[x]|h|r"));
    True("glob star alone", PbpGlobMatch("*", "anything"));
    True("glob star empty text", PbpGlobMatch("*", ""));
    True("glob empty pattern", !PbpGlobMatch("", ""));
    True("glob backtracks", PbpGlobMatch("*ab", "aab"));

    // ---- split ---------------------------------------------------------------------------
    {
        const std::vector<std::string> parts = PbpSplit(" a; b ;;c ", ';');
        True("split count", parts.size() == 3);
        EqS("split first", parts.size() > 0 ? parts[0] : "", "a");
        EqS("split second", parts.size() > 1 ? parts[1] : "", "b");
        EqS("split third", parts.size() > 2 ? parts[2] : "", "c");
        True("split empty", PbpSplit("", ';').empty());
        True("split only separators", PbpSplit(" ; ;", ';').empty());
    }

    // ---- muted lines: the default ---------------------------------------------------------
    {
        const std::string value = ConfValue(conf, "PlayerbotsPlus.Mute.Patterns");
        EqS("conf default is the code's", value, SourceLiteral(config, "kDefaultMutePatterns"));
        const std::vector<std::string> patterns = PbpSplit(value, ';');
        True("default patterns", patterns.size() == 8);
        True("mute item use", Muted(patterns, "Using |cffffffff|Hitem:6948:0:0:0:0:0:0:0:5|h[Hearthstone]|h|r"));
        True("mute drinking",
             Muted(patterns, "Drinking |cffffffff|Hitem:5350:0:0:0:0:0:0:0:5|h[Conjured Water]|h|r (40%)"));
        True("mute eating", Muted(patterns, "Eating |cffffffff|Hitem:4540:0:0:0:0:0:0:0:5|h[Tough Hunk of Bread]|h|r"));
        True("mute ai reset", Muted(patterns, "AI was reset to defaults"));
        True("mute equipping", Muted(patterns, "Equipping |cff1eff00|Hitem:15357:0:0:0:0:0:0:0:20|h[Vigorous "
                                               "Handguards]|h|r"));
        True("mute equipping a hand",
             Muted(patterns, "Equipping |cff1eff00|Hitem:2489:0:0:0:0:0:0:0:20|h[Two-Handed Sword]|h|r in main hand"));
        True("mute opened", Muted(patterns, "Opened item: Satchel of Helpful Goods"));
        True("keep an outfit", !Muted(patterns, "Equipping outfit pvp"));
        True("mute dungeon", Muted(patterns, "I don't know this dungeon, lead the way!"));
        // Without a chat module these are all a bot says about an order or a
        // group: they stay.
        True("keep following", !Muted(patterns, "Following"));
        True("keep staying", !Muted(patterns, "Staying"));
        True("keep hello", !Muted(patterns, "Hello!"));
        True("keep grinding", !Muted(patterns, "Grinding"));
        True("keep quest progress",
             !Muted(patterns, "Kobold Vermin 3/10 |cFFFFFF00|Hquest:7:5|h[Kobold Camp Cleanup]|h|r"));
    }

    // ---- muted lines: with mod-ollama-chat ------------------------------------------------
    {
        const std::string value = ConfValue(conf, "#PlayerbotsPlus.Mute.Patterns");
        const std::vector<std::string> patterns = PbpSplit(value, ';');
        True("chat list there", patterns.size() > 30);
        for (std::string const& p : PbpSplit(ConfValue(conf, "PlayerbotsPlus.Mute.Patterns"), ';'))
        {
            bool has = false;
            for (std::string const& q : patterns)
                has = has || q == p;
            True("chat list has the default's " + p, has);
        }
        True("mute kill progress",
             Muted(patterns, "Kobold Vermin 3/10 |cFFFFFF00|Hquest:7:5|h[Kobold Camp Cleanup]|h|r"));
        True("mute item progress",
             Muted(patterns, "|cffffffff|Hitem:723:0:0:0:0:0:0:0:5|h[Goretusk Liver]|h|r 2/8 "
                             "|cFFFFFF00|Hquest:22:12|h[Goretusk Liver Pie]|h|r"));
        True("mute quest completed",
             Muted(patterns, "Quest completed |cFFFFFF00|Hquest:7:5|h[Kobold Camp Cleanup]|h|r"));
        True("mute accepted", Muted(patterns, "I have accepted |cFFFFFF00|Hquest:7:5|h[Kobold Camp Cleanup]|h|r"));
        True("mute staying", Muted(patterns, "Staying"));
        True("mute following", Muted(patterns, "Following"));
        True("mute hello follow", Muted(patterns, "Hello, I follow you!"));
        True("mute hi lead", Muted(patterns, "Hi, lead the way!"));
        True("mute grinding", Muted(patterns, "Grinding"));
        True("mute bye", Muted(patterns, "See you later!"));
        True("mute pleasure", Muted(patterns, "A pleasure doing business with you"));
        True("mute flight", Muted(patterns, "I am ready for the next flight"));
        True("mute cant do", Muted(patterns, "I can't do this right now"));
        // The quest whispers as you take and hand quests in, playerbots' own words.
        const std::string q = "|cFFFFFF00|Hquest:2984:20|h[Call of Fire]|h|r";
        True("mute quest available", Muted(patterns, "Quest |cff00ff00Available|r: " + q));
        True("mute quest incomplete", Muted(patterns, "Quest |cffff0000Incompleted|r: " + q));
        True("mute cant take", Muted(patterns, "Can't take " + q));
        True("mute accepted quest", Muted(patterns, "Accepted " + q));
        True("mute already on", Muted(patterns, "Already on " + q));
        True("mute already completed", Muted(patterns, "Already completed " + q));
        True("mute log full", Muted(patterns, "Quest log is full " + q));
        True("mute bags full", Muted(patterns, "Bags are full " + q));
        True("mute cannot accept", Muted(patterns, "Cannot accept " + q));
        True("mute handed in", Muted(patterns, "Quest I have completed the quest " + q + ": " + q));
        True("mute rewarded", Muted(patterns, "Quest Rewarded |cff1eff00|Hitem:6070:0:0:0:0:0:0:0:20|h[Wolfskin "
                                              "Bracers]|h|r: " + q));
        True("mute removed from a full log", Muted(patterns, "Quest removed " + q));
        True("mute confirm", Muted(patterns, "Quest: " + q + " confirm accept"));
        True("mute quest accepted", Muted(patterns, "Quest accepted"));
        True("mute quest shared", Muted(patterns, "Quest shared"));
        True("mute have this quest", Muted(patterns, "I have this quest"));
        True("mute cant take this quest", Muted(patterns, "I can't take this quest"));
        // ...but not what a bot answers when asked about its quests, nor a
        // reward it waits for you to pick.
        True("keep quests list", !Muted(patterns, q));
        True("keep quests heading", !Muted(patterns, "--- Incompleted quests ---"));
        True("keep query", !Muted(patterns, "--- " + q + " |c0000FF00completed|r ---"));
        True("keep dropped on request", !Muted(patterns, "Quest removed"));
        True("keep choose reward", !Muted(patterns, "Choose reward: |cff1eff00|Hitem:6070:0:0:0:0:0:0:0:20|h[Wolfskin "
                                                    "Bracers]|h|r"));
        True("keep a quest named in chat", !Muted(patterns, "anyone done " + q + " yet?"));
        True("keep plain ratio", !Muted(patterns, "im 3/10 on that"));
        True("keep thanks for", !Muted(patterns, "Thanks for the water"));
        True("keep staying here", !Muted(patterns, "staying here till you are back"));
    }

    // ---- playerbots' error lines ---------------------------------------------------------
    {
        std::vector<std::string> names;
        std::string text;
        True("error line", PbpSplitErrorLine("Mungehdo|cfff00000: I can't do this right now", names, text) &&
                               names.size() == 1 && names[0] == "Mungehdo" && text == "I can't do this right now");
        True("error line, two", PbpSplitErrorLine("Mungehdo, Milino|cfff00000: I am in combat", names, text) &&
                                    names.size() == 2 && names[1] == "Milino" && text == "I am in combat");
        True("not an error line", !PbpSplitErrorLine("Welcome to Dangaming!", names, text));
        True("no names", !PbpSplitErrorLine("|cfff00000: text", names, text));
    }

    // ---- hangout spot lines --------------------------------------------------------------
    {
        PbpSpotLine line;
        True("spot parses", PbpParseSpotLine("0 -8833.4 628.6 94.0 22 14 the Trade District in Stormwind", line));
        True("spot fields", line.map == 0 && line.x < -8833.0f && line.y > 628.0f && line.z > 93.9f &&
                            line.radius == 22.0f && line.capacity == 14);
        EqS("spot name", line.name, "the Trade District in Stormwind");
        PbpSpotLine noName;
        True("spot without a name", PbpParseSpotLine("571 5813.4 453.4 658.8 16 8", noName) && noName.name.empty());
        PbpSpotLine clamp;
        True("spot radius clamped", PbpParseSpotLine("1 1 2 3 500 4 far", clamp) && clamp.radius == 60.0f);
        True("spot radius floor", PbpParseSpotLine("1 1 2 3 0.5 4 near", clamp) && clamp.radius == 3.0f);
        True("spot capacity capped", PbpParseSpotLine("1 1 2 3 10 999 big", clamp) && clamp.capacity == 200);
        PbpSpotLine bad;
        True("spot missing numbers", !PbpParseSpotLine("0 -8833.4 628.6 the Trade District", bad));
        True("spot negative map", !PbpParseSpotLine("-1 1 2 3 10 4 x", bad));
        True("spot negative capacity", !PbpParseSpotLine("0 1 2 3 10 -4 x", bad));
        True("spot empty", !PbpParseSpotLine("", bad));
        PbpSpotLine apostrophe;
        True("spot with an apostrophe",
             PbpParseSpotLine("571 5813.4 453.4 658.8 16 8 Krasus' Landing in Dalaran", apostrophe) &&
                 apostrophe.name == "Krasus' Landing in Dalaran");

        // Every spot the conf ships reads, is a sane place in a city, and is
        // one of the code's defaults, word for word -- and the code has no
        // others.
        std::istringstream in(conf);
        std::string text;
        int spots = 0, good = 0, same = 0;
        const std::set<std::string> cities = { "Stormwind", "Ironforge", "Darnassus", "Exodar", "Orgrimmar",
                                               "Undercity", "Thunder Bluff", "Silvermoon", "Shattrath", "Dalaran" };
        std::map<std::string, int> room;   // people's worth, by city
        const std::string prefix = "PlayerbotsPlus.Hangout.Spot.";
        while (std::getline(in, text))
        {
            if (text.rfind(prefix, 0) != 0)
                continue;
            ++spots;
            const std::string key = text.substr(prefix.size(), text.find(' ') - prefix.size());
            const size_t q1 = text.find('"'), q2 = text.rfind('"');
            const std::string value = q1 != std::string::npos && q2 > q1 ? text.substr(q1 + 1, q2 - q1 - 1) : "";
            PbpSpotLine s;
            if (PbpParseSpotLine(value, s) && (s.map == 0 || s.map == 1 || s.map == 530 || s.map == 571) &&
                s.capacity > 0 && s.radius >= 10.0f && !s.name.empty() && cities.count(PbpCityOf(s.name)))
            {
                ++good;
                room[PbpCityOf(s.name)] += s.capacity;
            }
            else
                std::printf("   bad spot: %s\n", text.c_str());
            if (config.find("{ \"" + key + "\",") != std::string::npos &&
                config.find("\"" + value + "\" }") != std::string::npos)
                ++same;
            else
                std::printf("   not the code's default: %s\n", text.c_str());
        }
        True("conf spots", spots == 31 && good == 31);
        True("conf spots are the code's", same == 31);
        const size_t list = config.find("kDefaultHangoutSpots[]");
        const size_t end  = config.find("};", list);
        int defaults = 0;
        for (size_t at = config.find("\n        { \"", list); at < end; at = config.find("\n        { \"", at + 1))
            ++defaults;
        True("the code has no other spots: " + std::to_string(defaults), list != std::string::npos && defaults == 31);
        // Where people meet, there is room for them: Stormwind as much as
        // Orgrimmar, Dalaran (only from level 68) a good part of that.
        True("room in Orgrimmar: " + std::to_string(room["Orgrimmar"]), room["Orgrimmar"] >= 80);
        True("room in Stormwind: " + std::to_string(room["Stormwind"]), room["Stormwind"] >= room["Orgrimmar"]);
        True("room in Dalaran: " + std::to_string(room["Dalaran"]), room["Dalaran"] >= 60);
    }

    // ---- the places, shared out by city weight and hot spot ------------------------------
    {
        auto list = [](std::vector<uint32_t> const& v)
        {
            std::string out;
            for (uint32_t n : v)
                out += (out.empty() ? "" : ",") + std::to_string(n);
            return out;
        };
        // Stormwind: three hot spots and two others; Ironforge: one hot spot only;
        // Dalaran: nothing hot; Goldshire: no weight.
        const std::vector<PbpSpotShare> spots = {
            { "Stormwind", 20, true }, { "Stormwind", 14, true }, { "Stormwind", 12, true },
            { "Stormwind", 16, false }, { "Stormwind", 8, false },
            { "Ironforge", 12, true },
            { "Dalaran", 18, false }, { "Dalaran", 10, false },
            { "Goldshire", 7, false },
        };
        const std::map<std::string, uint32_t> weights = { { "Stormwind", 5 }, { "Ironforge", 2 }, { "Dalaran", 3 } };
        const std::vector<uint32_t> got = PbpShareHangoutPlaces(spots, weights, 100, 75);
        EqS("shared out", list(got), "16,12,10,8,4,20,19,11,7");
        uint32_t sw = 0, swHot = 0;
        for (size_t i = 0; i < 5; ++i)
        {
            sw += got[i];
            swHot += spots[i].hot ? got[i] : 0;
        }
        True("a city by its weight", sw == 50 && got[5] == 20 && got[6] + got[7] == 30);
        True("three quarters hot", swHot * 4 == sw * 3 + 2);   // 38 of 50, rounded
        True("one kind takes all", got[5] == 20);
        True("no weight: as written", got[8] == 7);

        // Places 0: what the weighted spots are written for (110 here).
        const std::vector<uint32_t> same = PbpShareHangoutPlaces(spots, weights, 0, 75);
        uint32_t total = 0;
        for (size_t i = 0; i < 8; ++i)
            total += same[i];
        True("places 0: the written total", total == 110);

        // A weight of 0: nobody there.
        const std::vector<uint32_t> none = PbpShareHangoutPlaces(spots, { { "Stormwind", 5 }, { "Ironforge", 0 },
                                                                          { "Dalaran", 5 } }, 100, 75);
        True("weight 0: nobody", none[5] == 0 && none[0] + none[1] + none[2] + none[3] + none[4] == 50);
        // Hot spots all written 0: by turns.
        const std::vector<uint32_t> turns = PbpShareHangoutPlaces({ { "Ironforge", 0, true }, { "Ironforge", 0, true } },
                                                                  { { "Ironforge", 1 } }, 9, 75);
        EqS("by turns", list(turns), "5,4");
        // 100%: only the hot spots; 0%: only the others.
        EqS("all hot", list(PbpShareHangoutPlaces({ spots[0], spots[3] }, { { "Stormwind", 1 } }, 10, 100)), "10,0");
        EqS("none hot", list(PbpShareHangoutPlaces({ spots[0], spots[3] }, { { "Stormwind", 1 } }, 10, 0)), "0,10");

        // ---- a full spot's room given out ----------------------------------------------
        // Stormwind's bank (hot) full at 5 of 12: its 7 go to Stormwind's other hot
        // spot (20), not its quiet one (10). Darnassus full at 0: its 24 go to every
        // open spot there is, by room (Stormwind's two, Orgrimmar's).
        const std::vector<PbpSpotRoom> rooms = {
            { "Stormwind", 20, true, false, 0 }, { "Stormwind", 12, true, true, 5 }, { "Stormwind", 10, false, false, 0 },
            { "Darnassus", 24, true, true, 0 },
            { "Orgrimmar", 30, true, false, 0 },
        };
        const std::vector<uint32_t> given = PbpShareFullRoom(rooms);
        EqS("full room given out", list(given), "35,5,14,0,42");
        uint32_t before = 0, after = 0;
        for (size_t i = 0; i < rooms.size(); ++i)
        {
            before += rooms[i].room;
            after += given[i];
        }
        True("nothing lost", before == after);
        // Its city's hot spots all full: to the city's quiet ones.
        EqS("hot full, to the quiet", list(PbpShareFullRoom({ { "Stormwind", 20, true, true, 18 },
                                                               { "Stormwind", 10, false, false, 0 },
                                                               { "Orgrimmar", 30, true, false, 0 } })),
            "18,12,30");
        // Nothing full: as it was.
        EqS("nothing full", list(PbpShareFullRoom({ { "Stormwind", 20, true, false, 0 },
                                                     { "Ironforge", 9, true, false, 0 } })),
            "20,9");
        // Held more than its room (the room went down since): it keeps its room.
        EqS("held above its room", list(PbpShareFullRoom({ { "Stormwind", 6, true, true, 9 },
                                                            { "Ironforge", 9, true, false, 0 } })),
            "6,9");
        // Everyone full: each keeps what it held, the rest has nowhere to go.
        EqS("all full", list(PbpShareFullRoom({ { "Stormwind", 20, true, true, 12 }, { "Ironforge", 9, true, true, 3 } })),
            "12,3");
        // A spot with no room of its own (switched off) takes none.
        EqS("no room takes none", list(PbpShareFullRoom({ { "Exodar", 8, true, true, 2 }, { "Ironforge", 0, true, false, 0 },
                                                          { "Dalaran", 10, false, false, 0 } })),
            "2,0,16");
    }

    // ---- the city a spot is in ----------------------------------------------------------
    {
        EqS("city after in", PbpCityOf("the Trade District in Stormwind"), "Stormwind");
        EqS("city of two words", PbpCityOf("outside the bank in Thunder Bluff"), "Thunder Bluff");
        EqS("city after of", PbpCityOf("outside the gates of Orgrimmar"), "Orgrimmar");
        EqS("city after outside", PbpCityOf("the zeppelin tower outside Orgrimmar"), "Orgrimmar");
        EqS("city without the", PbpCityOf("the main hall of the Exodar"), "Exodar");
        EqS("the last in", PbpCityOf("the Valley of Honor in Orgrimmar"), "Orgrimmar");
        EqS("the Undercity", PbpCityOf("the Trade Quarter in the Undercity"), "Undercity");
        EqS("an apostrophe", PbpCityOf("Krasus' Landing in Dalaran"), "Dalaran");
        EqS("no marker", PbpCityOf("Goldshire"), "Goldshire");
        EqS("nothing after it", PbpCityOf("something in "), "something in ");

        // Every spot the conf ships is in a city by that name.
        const std::vector<std::string> known = { "Stormwind", "Ironforge", "Orgrimmar", "Undercity", "Thunder Bluff",
                                                 "Darnassus", "Exodar", "Silvermoon", "Shattrath", "Dalaran" };
        std::istringstream in(conf);
        std::string text;
        int spots = 0, named = 0;
        while (std::getline(in, text))
        {
            if (text.rfind("PlayerbotsPlus.Hangout.Spot.", 0) != 0)
                continue;
            ++spots;
            const size_t q1 = text.find('"'), q2 = text.rfind('"');
            PbpSpotLine s;
            if (q1 != std::string::npos && q2 > q1 && PbpParseSpotLine(text.substr(q1 + 1, q2 - q1 - 1), s) &&
                std::find(known.begin(), known.end(), PbpCityOf(s.name)) != known.end())
                ++named;
            else
                std::printf("   no city: %s\n", text.c_str());
        }
        True("every conf spot has its city", spots > 0 && named == spots);
    }

    // ---- hunters' pet names and companions: what the conf ships is the code's --------
    {
        for (char const* side : { "Alliance", "Horde", "Both" })
        {
            const std::string key   = std::string("PlayerbotsPlus.Companions.") + side;
            const std::string value = ConfValue(conf, key);
            EqS(key + " is the code's", value, SourceLiteral(config, std::string("kDefaultCompanions") + side));
            // item, item:level, item:level:weight or item:level:weight:holiday
            bool readable = !value.empty();
            for (std::string const& part : PbpSplit(value, '|'))
            {
                std::smatch m;
                readable = readable &&
                           std::regex_match(part, m, std::regex("[0-9]+(:[0-9]+(:[1-9][0-9]*(:([a-z]+))?)?)?")) &&
                           (!m[4].matched || PbpHolidayKey(m[4].str()) != 0);
            }
            True(key + " reads", readable);
        }

        // The holidays a companion can have: known ones, case ignored, in
        // the order the game's are matched to.
        {
            True("holiday keys", PbpHolidayKeys().size() == 10 && PbpHolidayKey("winterveil") == 1 &&
                                     PbpHolidayKey("Brewfest") == 7 && PbpHolidayKey("darkmoon") == 10 &&
                                     PbpHolidayKey("christmas") == 0 && PbpHolidayKey("") == 0);
        }

        // Every Names line in the conf is one of the code's, word for word,
        // and every name is one the game takes.
        std::istringstream in(conf);
        std::string text;
        int lines = 0, same = 0, names = 0, good = 0;
        const std::string prefix = "PlayerbotsPlus.Pets.Names.";
        while (std::getline(in, text))
        {
            if (text.rfind(prefix, 0) != 0)
                continue;
            ++lines;
            const std::string family = text.substr(prefix.size(), text.find(' ') - prefix.size());
            const size_t q1 = text.find('"'), q2 = text.rfind('"');
            const std::string value = q1 != std::string::npos && q2 > q1 ? text.substr(q1 + 1, q2 - q1 - 1) : "";
            std::string joined;
            const size_t at = config.find("{ \"" + family + "\",");
            if (at != std::string::npos)
            {
                const size_t end = config.find('}', at);
                const std::regex literal("\"((?:[^\"\\\\]|\\\\.)*)\"");
                const std::string part = config.substr(at, end - at);
                int n = 0;
                for (std::sregex_iterator it(part.begin(), part.end(), literal), stop; it != stop; ++it, ++n)
                    if (n)   // the first is the family
                        joined += (*it)[1].str();
            }
            if (joined == value)
                ++same;
            else
                std::printf("   not the code's default: %s\n", family.c_str());
            for (std::string const& name : PbpSplit(value, '|'))
            {
                ++names;
                if (name.size() >= 2 && name.size() <= 12 &&
                    std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalpha(c) != 0; }))
                    ++good;
                else
                    std::printf("   not a name the game takes: %s\n", name.c_str());
            }
        }
        True("conf pet name families", lines == 33 && same == 33);
        True("conf pet names are all names the game takes", names > 300 && good == names);
    }

    // ---- the rest of the conf: every key the code reads is in it -------------------------
    {
        const std::regex key("\"(PlayerbotsPlus\\.[A-Za-z.]+)\"");
        int keys = 0;
        for (std::sregex_iterator it(config.begin(), config.end(), key), stop; it != stop; ++it)
        {
            const std::string name = (*it)[1].str();
            if (name.back() == '.')
                continue;   // a prefix (Hangout.Spot.)
            ++keys;
            True("in the conf: " + name, conf.find("\n" + name + " = ") != std::string::npos);
        }
        True("keys read", keys > 60);
    }

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
