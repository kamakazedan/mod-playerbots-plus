// Tests for the text helpers (src/PlayerbotsPlusText.cpp), which have no game
// types in them, and for what the conf ships: every hangout spot reads, the
// mute patterns mute what they should and nothing else, and the defaults in
// the code are the ones the conf says. Not part of the server build:
// AzerothCore only compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusText.h"

#include <cstdio>
#include <fstream>
#include <regex>
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

        // Every spot the conf ships reads, is a sane place, and is one of the
        // code's defaults, word for word.
        std::istringstream in(conf);
        std::string text;
        int spots = 0, good = 0, same = 0;
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
                s.capacity > 0 && s.radius >= 10.0f && !s.name.empty())
                ++good;
            else
                std::printf("   bad spot: %s\n", text.c_str());
            if (config.find("{ \"" + key + "\",") != std::string::npos &&
                config.find("\"" + value + "\" }") != std::string::npos)
                ++same;
            else
                std::printf("   not the code's default: %s\n", text.c_str());
        }
        True("conf spots", spots == 14 && good == 14);
        True("conf spots are the code's", same == 14);
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
