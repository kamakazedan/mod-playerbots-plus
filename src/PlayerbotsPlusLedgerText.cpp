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

        std::string bands;
        for (uint32_t b = 0; b < kBands; ++b)
        {
            const std::string key = std::to_string(b);
            bands += (bands.empty() ? "" : ", ") + BandName(b) + " " +
                     Average(N("now.ilvl.b" + key), N("now.geared.b" + key)) + " (" + Count(N("now.bots.b" + key)) +
                     " bots)";
        }
        out.push_back("Average item level by level: " + bands + ".");

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
