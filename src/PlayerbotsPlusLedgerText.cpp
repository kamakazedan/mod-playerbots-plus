#include "PlayerbotsPlusLedgerText.h"

#include <algorithm>
#include <cctype>

namespace PbpLedger
{
    bool IsGear(uint32_t itemClass)
    {
        return itemClass == ItemClass::Weapon || itemClass == ItemClass::Armor;
    }

    std::string ThingKind(uint32_t itemClass, uint32_t subClass, uint32_t quality)
    {
        if (quality == Quality::Poor)
            return "junk";
        if (IsGear(itemClass))
        {
            if (quality >= Quality::Epic)
                return "gear.epic";
            if (quality == Quality::Rare)
                return "gear.blue";
            if (quality == Quality::Uncommon)
                return "gear.green";
            return "other";
        }
        if (itemClass == ItemClass::Gem)
            return "gem";
        if (itemClass == ItemClass::Consumable)
            return "consumable";
        if (itemClass != ItemClass::TradeGoods)
            return "other";
        switch (subClass)
        {
            case TradeSub::Herb:          return "herb";
            case TradeSub::MetalStone:    return "ore";
            case TradeSub::Leather:       return "leather";
            case TradeSub::Cloth:         return "cloth";
            case TradeSub::Meat:          return "meat";
            case TradeSub::Elemental:     return "elemental";
            case TradeSub::Enchanting:    return "enchanting";
            case TradeSub::Jewelcrafting: return "gem";
            case TradeSub::Parts:         return "parts";
            default:                      return "goods";
        }
    }

    std::string UseKind(uint32_t itemClass, uint32_t subClass)
    {
        if (itemClass != ItemClass::Consumable)
            return {};
        switch (subClass)
        {
            case ConsumableSub::Potion:      return "potion";
            case ConsumableSub::Elixir:      return "elixir";
            case ConsumableSub::Flask:       return "flask";
            case ConsumableSub::Food:        return "food";
            case ConsumableSub::Bandage:     return "bandage";
            case ConsumableSub::Scroll:      return "scroll";
            case ConsumableSub::Enhancement: return "enhancement";
            default:                         return "other";
        }
    }

    std::string AuctionKind(uint32_t itemClass)
    {
        if (IsGear(itemClass))
            return "gear";
        if (itemClass == ItemClass::TradeGoods || itemClass == ItemClass::Gem)
            return "goods";
        return "other";
    }

    uint32_t Band(uint32_t level)
    {
        if (level >= 80)
            return 5;
        if (level >= 70)
            return 4;
        if (level >= 60)
            return 3;
        if (level >= 40)
            return 2;
        if (level >= 20)
            return 1;
        return 0;
    }

    std::string BandName(uint32_t band)
    {
        static const char* const kNames[kBands] = { "1-19", "20-39", "40-59", "60-69", "70-79", "80" };
        return band < kBands ? kNames[band] : "?";
    }

    std::string DayOf(std::time_t when)
    {
        std::tm local{};
        localtime_r(&when, &local);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &local);
        return buf;
    }

    std::string WhenText(std::time_t when)
    {
        std::tm local{};
        localtime_r(&when, &local);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%d %b %H:%M", &local);
        std::string out = buf;
        if (!out.empty() && out[0] == '0')
            out.erase(0, 1);
        return out;
    }

    std::vector<uint64_t> GuidList(std::string const& text)
    {
        std::vector<uint64_t> out;
        std::string word;
        auto flush = [&]()
        {
            if (!word.empty() && word.size() <= 19 &&
                std::all_of(word.begin(), word.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                out.push_back(std::stoull(word));
            word.clear();
        };
        for (char c : text)
        {
            if (c == ',' || c == ' ' || c == ';' || c == '\t')
                flush();
            else
                word += c;
        }
        flush();
        return out;
    }

    std::string Count(int64_t n)
    {
        const bool negative = n < 0;
        uint64_t v = negative ? uint64_t(-(n + 1)) + 1 : uint64_t(n);
        std::string digits = std::to_string(v);
        std::string out;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            if (i && (digits.size() - i) % 3 == 0)
                out += ',';
            out += digits[i];
        }
        return negative ? "-" + out : out;
    }

    std::string Gold(int64_t copper)
    {
        if (copper < 0)
            return "-" + Gold(-(copper + 1) + 1);
        if (copper >= 10000)
            return Count(copper / 10000) + "g";
        if (copper >= 100)
            return Count(copper / 100) + "s";
        return Count(copper) + "c";
    }

    namespace
    {
        int64_t Get(std::map<std::string, int64_t> const& m, std::string const& key)
        {
            auto it = m.find(key);
            return it != m.end() ? it->second : 0;
        }

        // A sum, and what that is a day.
        std::string AndADay(int64_t copper, uint32_t days)
        {
            std::string out = Gold(copper);
            if (days > 1)
                out += ", about " + Gold(copper / int64_t(days)) + " a day";
            return out;
        }

        // An average item level, "-" with nothing to go by.
        std::string Average(int64_t sum, int64_t count)
        {
            return count > 0 ? std::to_string((sum + count / 2) / count) : std::string("-");
        }
    }

    std::string AnHour(int64_t copper, int64_t minutes)
    {
        return minutes > 0 ? Gold(copper * 60 / minutes) : std::string("-");
    }

    std::vector<std::string> ByBand(std::map<std::string, int64_t> const& flows)
    {
        std::vector<std::string> out;
        auto F = [&flows](std::string const& key) { return Get(flows, key); };
        for (uint32_t b = 0; b < kBands; ++b)
        {
            const std::string k = ".b" + std::to_string(b);
            const int64_t online = F("time" + k), playing = F("time.active" + k);
            if (online <= 0)
                continue;
            out.push_back(BandName(b) + ": " + Count(online / 60) + " bot-hours online, " + Count(playing / 60) +
                          " with their AI running -- an hour of play, got " + AnHour(F("gold.in" + k), playing) +
                          " (looted " + AnHour(F("gold.loot" + k), playing) + ", quest rewards " +
                          AnHour(F("gold.quests" + k), playing) + "), spent " +
                          AnHour(F("ah.bought.value" + k), playing) + " on the auction house; playerbots' top-ups " +
                          AnHour(F("gold.free" + k), online) + " an hour online (took back " +
                          AnHour(F("gold.gone" + k), online) + "); gear put on: handed out " +
                          Count(F("gear.handout" + k)) + ", looted " + Count(F("gear.looted" + k)) + ", quests " +
                          Count(F("gear.quest" + k)) + ", the auction house " + Count(F("gear.bought" + k)) +
                          ", vendors " + Count(F("gear.vendor" + k)) + ", made " + Count(F("gear.made" + k)) + ".");
        }
        return out;
    }

    namespace
    {
        // A leaning's or a pair's time and gold, the most an hour of play
        // first, none to go by last.
        struct KindRow
        {
            std::string name;
            int64_t     online  = 0;
            int64_t     playing = 0;
            int64_t     got     = 0;
        };

        std::vector<KindRow> KindRows(std::map<std::string, int64_t> const& flows, std::string const& kind)
        {
            std::vector<KindRow> rows;
            const std::string prefix = "time." + kind + ".";
            for (auto it = flows.lower_bound(prefix); it != flows.end() && it->first.rfind(prefix, 0) == 0; ++it)
            {
                const std::string name = it->first.substr(prefix.size());
                if (it->second > 0 && !name.empty())
                    rows.push_back({ name, it->second, Get(flows, "time.active." + kind + "." + name),
                                     Get(flows, "gold.in." + kind + "." + name) });
            }
            auto rate = [](KindRow const& r) { return r.playing > 0 ? r.got * 60 / r.playing : int64_t(-1); };
            std::stable_sort(rows.begin(), rows.end(),
                             [&rate](KindRow const& a, KindRow const& b) { return rate(a) > rate(b); });
            return rows;
        }
    }

    std::vector<std::string> ByKind(std::map<std::string, int64_t> const& flows, std::string const& kind)
    {
        std::vector<std::string> out;
        for (KindRow const& r : KindRows(flows, kind))
            out.push_back(r.name + ": " + Count(r.online / 60) + " bot-hours online, " + Count(r.playing / 60) +
                          " with their AI running -- an hour of play, got " + AnHour(r.got, r.playing) + ".");
        return out;
    }

    std::string KeyPart(std::string const& name)
    {
        std::string out;
        for (char c : name)
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '+')
                if (out.size() < 24)
                    out += c;
        return out;
    }

    std::vector<std::string> NowKeys()
    {
        std::vector<std::string> keys = { "now.time", "now.bots", "now.gold", "now.prof.gatherers",
                                          "now.prof.crafters", "now.prof.none", "now.lfg", "now.lfg.ilvl",
                                          "now.lfg.geared", "now.carried.goods", "now.carried.value",
                                          "now.bank.items", "now.ah.ahbot", "now.ah.ahbot.value", "now.ah.bot",
                                          "now.ah.bot.value", "now.ah.player", "now.ah.player.value" };
        for (uint32_t b = 0; b < kBands; ++b)
            for (const char* what : { "now.bots.b", "now.gold.b", "now.ilvl.b", "now.geared.b" })
                keys.push_back(what + std::to_string(b));
        for (Profession const& p : kProfessions)
            keys.push_back(std::string("now.prof.") + p.name);
        return keys;
    }

    std::vector<std::string> Brief(std::map<std::string, int64_t> const& flows,
                                   std::map<std::string, int64_t> const& now, uint32_t withData)
    {
        std::vector<std::string> out;
        if (flows.empty() && now.empty())
        {
            out.push_back("No figures yet: they start once random bots are online.");
            return out;
        }
        auto F = [&flows](std::string const& key) { return Get(flows, key); };
        const uint32_t per = std::max<uint32_t>(withData, 1);
        out.push_back("Gold in " + AndADay(F("gold.in"), per) + ", out " + AndADay(F("gold.out"), per) +
                      "; playerbots' own top-ups +" + Gold(F("gold.free")) + ", taken back -" + Gold(F("gold.gone")) +
                      ".");
        out.push_back("Auction house: " + Count(F("ah.listed")) + " listed, " + Count(F("ah.sold")) + " sold for " +
                      Gold(F("ah.sold.value")) + ", " + Count(F("ah.bought")) + " bought for " +
                      Gold(F("ah.bought.value")) + ", " + Count(F("ah.expired")) + " back unsold.");
        std::string hour;
        for (uint32_t b = 0; b < kBands; ++b)
        {
            const std::string k = ".b" + std::to_string(b);
            const int64_t playing = F("time.active" + k);
            if (playing > 0)
                hour += (hour.empty() ? "" : ", ") + BandName(b) + " " + AnHour(F("gold.in" + k), playing) + " (" +
                        AnHour(F("gold.loot" + k), playing) + ")";
        }
        if (!hour.empty())
            out.push_back("Got an hour of play (looted), by level: " + hour + ".");
        // ...and by the kind of person a bot is: whether the greedy earn more.
        std::string leanings;
        for (KindRow const& r : KindRows(flows, "lean"))
            if (r.playing > 0)
                leanings += (leanings.empty() ? "" : ", ") + r.name + " " + AnHour(r.got, r.playing);
        if (!leanings.empty())
            out.push_back("Got an hour of play, by leaning (every level together): " + leanings + ".");
        if (now.empty())
            out.push_back("No look at the bots yet since the server started.");
        else
        {
            auto N = [&now](std::string const& key) { return Get(now, key); };
            const int64_t bots = N("now.bots");
            out.push_back("Last look at the bots (" + WhenText(std::time_t(N("now.time"))) + "): " + Count(bots) +
                          " online, holding " + Gold(N("now.gold")) + ", " + Gold(bots ? N("now.gold") / bots : 0) +
                          " each on average.");
        }
        return out;
    }

    std::vector<std::string> Report(std::string const& title, std::map<std::string, int64_t> const& flows,
                                    std::map<std::string, int64_t> const& now, uint32_t withData)
    {
        std::vector<std::string> out;
        out.push_back(title);
        if (flows.empty() && now.empty())
        {
            out.push_back("No figures yet: they start once random bots are online.");
            return out;
        }

        auto F = [&flows](std::string const& key) { return Get(flows, key); };
        const uint32_t per = std::max<uint32_t>(withData, 1);

        // ---- gold ----
        const int64_t in = F("gold.in"), loot = F("gold.loot"), quests = F("gold.quests"), mail = F("gold.mail");
        out.push_back("Gold in: " + AndADay(in, per) + " -- looted " + Gold(loot) + ", quest rewards " +
                      Gold(quests) + ", auction house mail " + Gold(mail) + ", the rest " +
                      Gold(std::max<int64_t>(0, in - loot - quests - mail)) +
                      " (selling to vendors, other mail, trades).");
        const int64_t gout = F("gold.out"), buys = F("ah.bought.value"), deposits = F("ah.deposits");
        out.push_back("Gold out: " + AndADay(gout, per) + " -- auction house buys " + Gold(buys) + ", deposits " +
                      Gold(deposits) + ", the rest " + Gold(std::max<int64_t>(0, gout - buys - deposits)) +
                      " (vendors, repairs, training).");
        out.push_back("Gold playerbots gives or takes outside the game's own payments (its top-up whenever it "
                      "refreshes a bot, level resets): +" + Gold(F("gold.free")) + ", -" + Gold(F("gold.gone")) +
                      ".");

        // ---- the auction house ----
        out.push_back("Auction house, selling: " + Count(F("ah.listed")) + " listed (" + Count(F("ah.listed.gear")) +
                      " gear, " + Count(F("ah.listed.goods")) + " goods, " + Count(F("ah.listed.other")) +
                      " other) asking " + Gold(F("ah.listed.value")) + "; " + Count(F("ah.sold")) + " sold for " +
                      Gold(F("ah.sold.value")) + " (" + Gold(F("ah.sold.cut")) + " of it to the house) -- to the AH bot " +
                      Count(F("ah.sold.ahbot")) + " (" + Gold(F("ah.sold.ahbot.value")) + "), to players " +
                      Count(F("ah.sold.player")) + " (" + Gold(F("ah.sold.player.value")) + "), to other bots " +
                      Count(F("ah.sold.bot")) + " (" + Gold(F("ah.sold.bot.value")) + "); " + Count(F("ah.expired")) +
                      " came back unsold.");
        out.push_back("Auction house, buying: " + Count(F("ah.bought")) + " bought for " + Gold(buys) +
                      " -- from the AH bot " + Count(F("ah.bought.ahbot")) + " (" + Gold(F("ah.bought.ahbot.value")) +
                      "), from players " + Count(F("ah.bought.player")) + " (" + Gold(F("ah.bought.player.value")) +
                      "), from other bots " + Count(F("ah.bought.bot")) + " (" + Gold(F("ah.bought.bot.value")) +
                      ").");

        // ---- things ----
        out.push_back("Looted: herbs " + Count(F("loot.herb")) + ", ore and stone " + Count(F("loot.ore")) +
                      ", leather " + Count(F("loot.leather")) + ", cloth " + Count(F("loot.cloth")) +
                      ", meat and fish " + Count(F("loot.meat")) + ", elemental " + Count(F("loot.elemental")) +
                      ", enchanting " + Count(F("loot.enchanting")) + ", gems " + Count(F("loot.gem")) + ", parts " +
                      Count(F("loot.parts")) + ", other goods " + Count(F("loot.goods")) + "; gear: green " +
                      Count(F("loot.gear.green")) + ", blue " + Count(F("loot.gear.blue")) + ", epic " +
                      Count(F("loot.gear.epic")) + "; grey junk " + Count(F("loot.junk")) + ". " +
                      Count(F("loot.nodes")) + " things in all came out of herbs, ore veins, chests and fishing.");
        auto three = [&F](std::string const& prefix)
        {
            const int64_t gear = F(prefix + ".gear.green") + F(prefix + ".gear.blue") + F(prefix + ".gear.epic");
            const int64_t goods = F(prefix + ".herb") + F(prefix + ".ore") + F(prefix + ".leather") +
                                  F(prefix + ".cloth") + F(prefix + ".meat") + F(prefix + ".elemental") +
                                  F(prefix + ".enchanting") + F(prefix + ".gem") + F(prefix + ".parts") +
                                  F(prefix + ".goods");
            const int64_t consumables = F(prefix + ".consumable");
            const int64_t rest = F(prefix + ".other") + F(prefix + ".junk");
            return Count(gear + goods + consumables + rest) + " -- consumables " + Count(consumables) + ", goods " +
                   Count(goods) + ", gear " + Count(gear) + ", other " + Count(rest);
        };
        out.push_back("Made (crafted or conjured): " + three("made") + ". Bought from vendors: " + three("vendor") +
                      ".");

        const int64_t handout = F("gear.handout"), looted = F("gear.looted"), quest = F("gear.quest"),
                      bought = F("gear.bought"), vendor = F("gear.vendor"), made = F("gear.made");
        out.push_back("Gear put on: " + Count(handout + looted + quest + bought + vendor + made) +
                      " -- handed out by playerbots " + Count(handout) + " (item level " +
                      Average(F("gear.handout.ilvl"), handout) + "), looted " + Count(looted) + " (" +
                      Average(F("gear.looted.ilvl"), looted) + "), quest rewards " + Count(quest) + " (" +
                      Average(F("gear.quest.ilvl"), quest) + "), from the auction house " + Count(bought) + " (" +
                      Average(F("gear.bought.ilvl"), bought) + "), from vendors " + Count(vendor) + ", made " +
                      Count(made) + ".");
        out.push_back("Used up: food and drink " + Count(F("used.food")) + ", potions " + Count(F("used.potion")) +
                      ", elixirs " + Count(F("used.elixir")) + ", flasks " + Count(F("used.flask")) + ", bandages " +
                      Count(F("used.bandage")) + ", scrolls " + Count(F("used.scroll")) +
                      ", oils, stones and poisons " + Count(F("used.enhancement")) + ", other " +
                      Count(F("used.other")) + ".");

        // ---- by level, an hour of a bot's time ----
        const std::vector<std::string> bands = ByBand(flows);
        if (!bands.empty())
        {
            out.push_back("By level (a bot's time counted at each look at them):");
            for (std::string const& line : bands)
                out.push_back("  " + line);
        }

        // ---- by the kind of person a bot is, and by its professions ----
        // Every level together: a leaning's bots are of every level, as
        // anyone's are, so a level band's worth is in all of them alike.
        const std::vector<std::string> leanings = ByKind(flows, "lean");
        if (!leanings.empty())
        {
            out.push_back("By leaning, the kind of person a bot is (every level together, the most an hour of play "
                          "first):");
            for (std::string const& line : leanings)
                out.push_back("  " + line);
        }
        const std::vector<std::string> pairs = ByKind(flows, "pair");
        if (!pairs.empty())
        {
            out.push_back("By profession pair (every level together, the most an hour of play first):");
            for (std::string const& line : pairs)
                out.push_back("  " + line);
        }

        // ---- the latest look at them ----
        if (now.empty())
        {
            out.push_back("No look at the bots yet since the server started.");
            return out;
        }
        auto N = [&now](std::string const& key) { return Get(now, key); };
        const int64_t bots = N("now.bots");
        const int64_t top = N("now.bots.b5");
        out.push_back("Last look at the bots (" + WhenText(std::time_t(N("now.time"))) + "): " + Count(bots) +
                      " online, holding " + Gold(N("now.gold")) + " -- " + Gold(bots ? N("now.gold") / bots : 0) +
                      " each on average, " + Gold(top ? N("now.gold.b5") / top : 0) + " at level 80.");

        std::string levels;
        for (uint32_t b = 0; b < kBands; ++b)
        {
            const std::string key = std::to_string(b);
            levels += (levels.empty() ? "" : ", ") + BandName(b) + " " +
                      Average(N("now.ilvl.b" + key), N("now.geared.b" + key)) + " (" + Count(N("now.bots.b" + key)) +
                      " bots)";
        }
        out.push_back("Average item level by level: " + levels + ".");

        std::string professions;
        for (Profession const& p : kProfessions)
            professions += (professions.empty() ? "" : ", ") + std::string(p.name) + " " +
                           Count(N(std::string("now.prof.") + p.name));
        out.push_back("Professions: " + professions + " -- " + Count(N("now.prof.gatherers")) +
                      " have a gathering one, " + Count(N("now.prof.crafters")) + " a crafting one, " +
                      Count(N("now.prof.none")) + " neither.");

        out.push_back("In the dungeon queue: " + Count(N("now.lfg")) + " (item level " +
                      Average(N("now.lfg.ilvl"), N("now.lfg.geared")) + "). Carrying " +
                      Count(N("now.carried.goods")) + " trade goods a vendor would give " +
                      Gold(N("now.carried.value")) + " for; " + Count(N("now.bank.items")) +
                      " things in their banks.");

        const int64_t ahBot = N("now.ah.ahbot"), ahBots = N("now.ah.bot"), ahPlayers = N("now.ah.player");
        out.push_back("Up on the auction houses: " + Count(ahBot + ahBots + ahPlayers) + " -- the AH bot " +
                      Count(ahBot) + " (asking " + Gold(N("now.ah.ahbot.value")) + "), bots " + Count(ahBots) +
                      " (" + Gold(N("now.ah.bot.value")) + "), players " + Count(ahPlayers) + " (" +
                      Gold(N("now.ah.player.value")) + ").");
        return out;
    }
}
