#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusMute.h"
#include "PlayerbotsPlusText.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

PbpSettings g_Pbp;

namespace pbp
{
    bool g_PbpDebug = false;

    bool        g_HangoutEnable           = true;
    std::vector<PbpHangoutSpot> g_HangoutSpots;
    uint32_t    g_HangoutCheckSeconds     = 5;
    float       g_HangoutRecruitRange     = 300.0f;
    uint32_t    g_HangoutRecruitChance    = 50;
    uint32_t    g_HangoutStayMinutesMin   = 5;
    uint32_t    g_HangoutStayMinutesMax   = 20;
    uint32_t    g_HangoutCooldownMinutes  = 45;
    uint32_t    g_HangoutTravelSeconds    = 180;
    uint32_t    g_HangoutMountedChance    = 45;
    uint32_t    g_HangoutBenchChance      = 60;
    uint32_t    g_HangoutSitChance        = 35;
    uint32_t    g_HangoutAfkChance        = 35;
    float       g_HangoutBenchRange       = 25.0f;
    float       g_HangoutHeightTolerance  = 6.0f;
    std::vector<std::string> g_HangoutBenchWords;
    bool        g_HangoutBringIn          = true;
    uint32_t    g_HangoutBringInFill      = 75;
    uint32_t    g_HangoutBringInPerMinute = 15;
    uint32_t    g_HangoutBringInMinLevel  = 10;
    float       g_HangoutArrivalRange     = 400.0f;

    bool        g_StuckEnable             = true;
    uint32_t    g_StuckMountCount         = 4;
    uint32_t    g_StuckWindowSeconds      = 30;
    float       g_StuckRadius             = 6.0f;
    uint32_t    g_StuckIgnoreMinutes      = 10;
    uint32_t    g_StuckWalkMinutes        = 10;

    bool        g_ReviveEnable              = true;
    uint32_t    g_ReviveReleaseDelaySeconds = 5;
    uint32_t    g_ReviveWaitForRezSeconds   = 60;
    uint32_t    g_ReviveRunBackSeconds      = 40;

    bool        g_MarksEnable                = true;
    std::string g_MarksClassIcon[16];
    bool        g_MarksAddCcStrategy         = true;
    bool        g_MarksSapStar               = true;
    bool        g_MarksCrossToSkull          = false;
    bool        g_MarksAutoSkull             = false;
    uint32_t    g_MarksInUseSeconds          = 180;
    uint32_t    g_MarksSapStealthWaitSeconds = 12;
    uint32_t    g_MarksSapApproachSeconds    = 40;
    uint32_t    g_MarksSapWaitSeconds        = 20;
}

namespace
{
    bool ToUInt(std::string const& s, uint32_t& out)
    {
        if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return false;
        try
        {
            const unsigned long v = std::stoul(s);
            out = uint32_t(std::min<unsigned long>(v, UINT32_MAX));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    uint32_t U(char const* key, uint32_t def)
    {
        return sConfigMgr->GetOption<uint32_t>(key, def);
    }

    bool B(char const* key, bool def)
    {
        return sConfigMgr->GetOption<bool>(key, def);
    }

    // "40:120" -> 40, 120; "60" -> 60, 60.
    void Range(char const* key, uint32_t& lo, uint32_t& hi, uint32_t defLo, uint32_t defHi)
    {
        const std::string def = std::to_string(defLo) + ":" + std::to_string(defHi);
        const std::vector<std::string> parts = PbpSplit(sConfigMgr->GetOption<std::string>(key, def), ':');
        uint32_t a = defLo, b = defHi;
        if (parts.size() == 1 && ToUInt(parts[0], a))
            b = a;
        else if (parts.size() != 2 || !ToUInt(parts[0], a) || !ToUInt(parts[1], b))
        {
            LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] {} should look like {}; using that", key, def);
            a = defLo;
            b = defHi;
        }
        lo = std::min(a, b);
        hi = std::max(a, b);
    }

    // "100,250,500" into an array, keeping the default for anything missing.
    template <size_t N>
    void List(char const* key, std::array<uint32_t, N>& out)
    {
        std::string def;
        for (size_t i = 0; i < N; ++i)
            def += (i ? "," : "") + std::to_string(out[i]);
        const std::vector<std::string> parts = PbpSplit(sConfigMgr->GetOption<std::string>(key, def), ',');
        for (size_t i = 0; i < N && i < parts.size(); ++i)
        {
            uint32_t v = 0;
            if (ToUInt(parts[i], v))
                out[i] = v;
            else
                LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] {}: '{}' is not a number", key, parts[i]);
        }
    }

    std::unordered_set<uint32_t> IdSet(char const* key, std::string const& def)
    {
        std::unordered_set<uint32_t> out;
        for (std::string const& s : PbpSplit(sConfigMgr->GetOption<std::string>(key, def), ','))
        {
            uint32_t v = 0;
            if (ToUInt(s, v) && v)
                out.insert(v);
            else
                LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] {}: '{}' is not an id", key, s);
        }
        return out;
    }
}

namespace
{
    float F(char const* key, float def)
    {
        return sConfigMgr->GetOption<float>(key, def);
    }

    // Where people stand about in each capital: "map x y z radius capacity
    // name". Used when the conf has no PlayerbotsPlus.Hangout.Spot.* keys.
    const std::pair<const char*, const char*> kDefaultHangoutSpots[] = {
        { "StormwindTrade",     "0 -8833.4 628.6 94.0 22 14 the Trade District in Stormwind" },
        { "StormwindBank",      "0 -8914.0 621.5 99.6 14 8 the bank in Stormwind" },
        { "IronforgeCommons",   "0 -4918.9 -940.4 501.6 22 12 the Commons in Ironforge" },
        { "OrgrimmarValley",    "1 1657.9 -4433.0 17.5 22 10 the Valley of Strength in Orgrimmar" },
        { "OrgrimmarBank",      "1 1615.6 -4391.6 10.3 14 6 outside the bank in Orgrimmar" },
        { "UndercityTrade",     "0 1584.1 240.3 -52.2 16 10 the Trade Quarter in the Undercity" },
        { "ThunderBluffBank",   "1 -1263.3 44.5 127.5 16 8 outside the bank in Thunder Bluff" },
        { "DarnassusBank",      "1 9943.0 2497.7 1317.7 16 8 outside the bank in Darnassus" },
        { "ExodarHall",         "530 -3965.7 -11653.6 -138.8 18 8 the main hall of the Exodar" },
        { "SilvermoonExchange", "530 9744.0 -7466.1 13.5 22 10 the Royal Exchange in Silvermoon" },
        { "ShattrathTerrace",   "530 -1863.7 5430.0 -9.6 24 12 the Terrace of Light in Shattrath" },
        { "DalaranBank",        "571 5958.1 617.3 650.6 18 12 outside the bank in Dalaran" },
        { "DalaranKrasus",      "571 5813.4 453.4 658.8 16 8 Krasus' Landing in Dalaran" },
        { "DalaranMerchants",   "571 5654.9 682.2 652.0 14 8 outside the Merchants' Bank in Dalaran" },
    };

    void LoadHangout()
    {
        using namespace pbp;
        g_HangoutEnable          = B("PlayerbotsPlus.Hangout.Enable", true);
        // At most 20: a hanging-out bot's playerbots "rest" (30 s) is renewed on these checks.
        g_HangoutCheckSeconds    = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.CheckSeconds", 5), 1, 20);
        g_HangoutRecruitRange    = std::max(20.0f, F("PlayerbotsPlus.Hangout.RecruitRange", 300.0f));
        g_HangoutRecruitChance   = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.RecruitChance", 50));
        // At most an hour: a parked bot's AI is held, and what playerbots
        // queues for it (emotes in range) waits until it goes.
        g_HangoutStayMinutesMin  = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.StayMinutesMin", 5), 1, 60);
        g_HangoutStayMinutesMax  = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.StayMinutesMax", 20),
                                                        g_HangoutStayMinutesMin, 60);
        g_HangoutCooldownMinutes = U("PlayerbotsPlus.Hangout.CooldownMinutes", 45);
        g_HangoutTravelSeconds   = std::max<uint32_t>(30, U("PlayerbotsPlus.Hangout.TravelSeconds", 180));
        g_HangoutMountedChance   = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.MountedChance", 45));
        g_HangoutBenchChance     = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.BenchChance", 60));
        g_HangoutSitChance       = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.SitChance", 35));
        g_HangoutAfkChance       = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.AfkChance", 35));
        g_HangoutBenchRange      = std::max(0.0f, F("PlayerbotsPlus.Hangout.BenchRange", 25.0f));
        g_HangoutHeightTolerance = std::max(1.0f, F("PlayerbotsPlus.Hangout.HeightTolerance", 6.0f));
        g_HangoutBenchWords.clear();
        const std::string benchWords = sConfigMgr->GetOption<std::string>("PlayerbotsPlus.Hangout.BenchWords", "bench");
        for (std::string word : PbpSplit(benchWords, '|'))
        {
            for (char& c : word)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            g_HangoutBenchWords.push_back(std::move(word));
        }
        g_HangoutBringIn          = B("PlayerbotsPlus.Hangout.BringIn", true);
        g_HangoutBringInFill      = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.BringInFill", 75));
        g_HangoutBringInPerMinute = std::min<uint32_t>(120, U("PlayerbotsPlus.Hangout.BringInPerMinute", 15));
        g_HangoutBringInMinLevel  = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.BringInMinLevel", 10), 1, 80);
        g_HangoutArrivalRange     = std::clamp(F("PlayerbotsPlus.Hangout.ArrivalRange", 400.0f), 0.0f, 1000.0f);

        g_HangoutSpots.clear();
        std::string bad;
        auto addSpot = [&bad](std::string const& key, std::string const& value)
        {
            PbpSpotLine line;
            if (!PbpParseSpotLine(value, line))
            {
                bad += (bad.empty() ? "" : ", ") + key;
                return;
            }
            PbpHangoutSpot spot;
            spot.key      = key;
            spot.name     = line.name.empty() ? key : line.name;
            spot.map      = line.map;
            spot.x        = line.x;
            spot.y        = line.y;
            spot.z        = line.z;
            spot.radius   = line.radius;
            spot.capacity = line.capacity;
            g_HangoutSpots.push_back(std::move(spot));
        };
        const std::string prefix = "PlayerbotsPlus.Hangout.Spot.";
        const std::vector<std::string> keys = sConfigMgr->GetKeysByString(prefix);
        if (keys.empty())
            for (auto const& [key, value] : kDefaultHangoutSpots)
                addSpot(key, value);
        for (std::string const& key : keys)
            addSpot(key.substr(prefix.size()), sConfigMgr->GetOption<std::string>(key, ""));
        if (!bad.empty())
            LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.Hangout.Spot: could not read {} (want "
                      "\"map x y z radius capacity name\").", bad);
    }

    void LoadStuckReviveMarks()
    {
        using namespace pbp;
        g_StuckEnable        = B("PlayerbotsPlus.Stuck.Enable", true);
        g_StuckMountCount    = std::max<uint32_t>(3, U("PlayerbotsPlus.Stuck.MountCount", 4));
        g_StuckWindowSeconds = std::max<uint32_t>(10, U("PlayerbotsPlus.Stuck.WindowSeconds", 30));
        g_StuckRadius        = std::max(1.0f, F("PlayerbotsPlus.Stuck.Radius", 6.0f));
        g_StuckIgnoreMinutes = std::max<uint32_t>(1, U("PlayerbotsPlus.Stuck.IgnoreMinutes", 10));
        g_StuckWalkMinutes   = std::min<uint32_t>(120, U("PlayerbotsPlus.Stuck.WalkMinutes", 10));

        g_ReviveEnable              = B("PlayerbotsPlus.Revive.Enable", true);
        g_ReviveReleaseDelaySeconds = U("PlayerbotsPlus.Revive.ReleaseDelaySeconds", 5);
        g_ReviveWaitForRezSeconds   = U("PlayerbotsPlus.Revive.WaitForRezSeconds", 60);
        g_ReviveRunBackSeconds      = U("PlayerbotsPlus.Revive.RunBackSeconds", 40);

        g_MarksEnable                = B("PlayerbotsPlus.Marks.Enable", true);
        g_MarksAddCcStrategy         = B("PlayerbotsPlus.Marks.AddCcStrategy", true);
        g_MarksSapStar               = B("PlayerbotsPlus.Marks.SapStar", true);
        // Off by default: the marks are the players'. Nothing here puts one on.
        g_MarksCrossToSkull          = B("PlayerbotsPlus.Marks.CrossToSkull", false);
        g_MarksAutoSkull             = B("PlayerbotsPlus.Marks.AutoSkull", false);
        g_MarksInUseSeconds          = U("PlayerbotsPlus.Marks.InUseSeconds", 180);
        g_MarksSapStealthWaitSeconds = U("PlayerbotsPlus.Marks.SapStealthWaitSeconds", 12);
        g_MarksSapApproachSeconds    = U("PlayerbotsPlus.Marks.SapApproachSeconds", 40);
        g_MarksSapWaitSeconds        = U("PlayerbotsPlus.Marks.SapWaitSeconds", 20);

        for (std::string& icon : g_MarksClassIcon)
            icon.clear();
        static const std::pair<const char*, uint8_t> classes[] = {
            { "warrior", 1 }, { "paladin", 2 }, { "hunter", 3 }, { "rogue", 4 }, { "priest", 5 },
            { "deathknight", 6 }, { "death knight", 6 }, { "dk", 6 }, { "shaman", 7 }, { "mage", 8 },
            { "warlock", 9 }, { "druid", 11 },
        };
        static const char* const icons[] = {
            "star", "circle", "diamond", "triangle", "moon", "square", "cross", "skull",
        };
        std::string bad;
        for (std::string const& part : PbpSplit(sConfigMgr->GetOption<std::string>("PlayerbotsPlus.Marks.ClassIcons",
                 "mage:moon, rogue:star, priest:circle, warlock:diamond, druid:triangle, hunter:square"), ','))
        {
            const size_t colon = part.find(':');
            std::string cls  = colon == std::string::npos ? std::string() : part.substr(0, colon);
            std::string icon = colon == std::string::npos ? std::string() : part.substr(colon + 1);
            auto clean = [](std::string& s)
            {
                const size_t b = s.find_first_not_of(" \t");
                const size_t e = s.find_last_not_of(" \t");
                s = b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
                for (char& ch : s)
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            };
            clean(cls);
            clean(icon);
            uint8_t id = 0;
            for (auto const& [name, value] : classes)
                if (cls == name)
                    id = value;
            bool known = false;
            for (const char* i : icons)
                known = known || icon == i;
            if (!id || !known)
            {
                bad += (bad.empty() ? "" : ", ") + part;
                continue;
            }
            g_MarksClassIcon[id] = icon;
        }
        if (!bad.empty())
            LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] PlayerbotsPlus.Marks.ClassIcons: not a class:mark "
                      "pair: {}", bad);
    }

    // Separated by ';': an item link in a line starts with '|'. Only lines
    // nobody needs: with a chat module that says the rest in its own words,
    // the conf mutes more ("Staying", "Hello!"; see the conf).
    constexpr char const* kDefaultMutePatterns =
        "AI was reset to defaults;Using |c*;Drinking |c*;Eating |c*;Feasting |c*;Equipping |c*;Opened item: *;"
        "I don't know this dungeon, lead the way!";
}

void PlayerbotsPlus_LoadConfig(bool reload)
{
    PbpSettings s;   // starts from the defaults in the header

    s.enable = B("PlayerbotsPlus.Enable", true);

    PbpMarketSettings& m = s.market;
    m.enable            = B("PlayerbotsPlus.Market.Enable", true);
    m.intervalMs        = std::max<uint32_t>(1, U("PlayerbotsPlus.Market.IntervalSeconds", 10)) * 1000;
    m.visitsPerInterval = std::clamp<uint32_t>(U("PlayerbotsPlus.Market.VisitsPerInterval", 1), 1, 20);
    Range("PlayerbotsPlus.Market.VisitCooldownMinutes", m.cooldownMinMinutes, m.cooldownMaxMinutes, 40, 120);
    m.minLevel          = U("PlayerbotsPlus.Market.MinLevel", 10);
    m.extraZones        = IdSet("PlayerbotsPlus.Market.ExtraZoneOrAreaIds", "3703,4395");

    m.sell                   = B("PlayerbotsPlus.Market.Sell.Enable", true);
    m.sellMinQuality         = std::min<uint32_t>(U("PlayerbotsPlus.Market.Sell.MinQuality", 1), 6);
    m.sellMaxQuality         = std::min<uint32_t>(U("PlayerbotsPlus.Market.Sell.MaxQuality", 4), 6);
    m.maxListingsPerVisit    = U("PlayerbotsPlus.Market.Sell.MaxPerVisit", 3);
    m.maxAuctionsPerBot      = U("PlayerbotsPlus.Market.Sell.MaxAuctionsPerBot", 8);
    m.maxBotAuctionsPerHouse = U("PlayerbotsPlus.Market.Sell.MaxBotAuctionsPerHouse", 2000);
    m.maxRelists             = U("PlayerbotsPlus.Market.Sell.MaxRelists", 2);
    List("PlayerbotsPlus.Market.Sell.DurationWeights", m.durationWeights);
    m.maxBuyoutGold          = std::max<uint32_t>(1, U("PlayerbotsPlus.Market.Sell.MaxBuyoutGold", 50000));

    m.buy                   = B("PlayerbotsPlus.Market.Buy.Enable", true);
    m.buyUpgrades           = B("PlayerbotsPlus.Market.Buy.Upgrades", true);
    m.buyMaterials          = B("PlayerbotsPlus.Market.Buy.Materials", true);
    m.maxBuysPerVisit       = U("PlayerbotsPlus.Market.Buy.MaxPerVisit", 2);
    m.buySharePct           = std::min<uint32_t>(U("PlayerbotsPlus.Market.Buy.MaxShareOfGoldPct", 25), 100);
    m.reservePerLevel       = U("PlayerbotsPlus.Market.Buy.ReserveCopperPerLevel", 2000);
    m.upgradeMinQuality     = std::min<uint32_t>(U("PlayerbotsPlus.Market.Buy.UpgradeMinQuality", 2), 6);
    m.upgradeLevelWindow    = U("PlayerbotsPlus.Market.Buy.UpgradeLevelWindow", 6);
    m.materialsOverValuePct = U("PlayerbotsPlus.Market.Buy.MaterialsMaxPctOfValue", 200);
    m.maxUsageChecks        = std::max<uint32_t>(1, U("PlayerbotsPlus.Market.MaxItemChecksPerVisit", 30));

    m.collectMail = B("PlayerbotsPlus.Market.CollectMail", true);
    m.equipBought = B("PlayerbotsPlus.Market.EquipBought", true);
    m.logTrades   = B("PlayerbotsPlus.Market.LogTrades", false);

    PbpPrice::Settings& p = m.price;
    List("PlayerbotsPlus.Market.Price.GearValuePct", p.gearValuePct);
    p.goodsValuePct     = U("PlayerbotsPlus.Market.Price.GoodsValuePct", 300);
    p.floorPct          = std::max<uint32_t>(U("PlayerbotsPlus.Market.Price.FloorPctOfVendor", 110), 100);
    p.wealthBaseCopper  = sConfigMgr->GetOption<float>("PlayerbotsPlus.Market.Price.WealthBaseCopper", 10000.0f);
    p.wealthGrowth      = sConfigMgr->GetOption<float>("PlayerbotsPlus.Market.Price.WealthGrowthLevels", 10.5f);
    List("PlayerbotsPlus.Market.Price.GearCapPctOfWealth", p.gearCapPct);
    p.stackCapPct       = U("PlayerbotsPlus.Market.Price.StackCapPctOfWealth", 20);
    Range("PlayerbotsPlus.Market.Price.UndercutPct", p.undercutMinPct, p.undercutMaxPct, 1, 5);
    p.undercutMaxPct    = std::min<uint32_t>(p.undercutMaxPct, 50);
    p.undercutMinPct    = std::min(p.undercutMinPct, p.undercutMaxPct);
    p.relistDiscountPct = std::min<uint32_t>(U("PlayerbotsPlus.Market.Price.RelistDiscountPct", 15), 50);

    if (m.sellMinQuality > m.sellMaxQuality)
        std::swap(m.sellMinQuality, m.sellMaxQuality);

    g_Pbp = std::move(s);

    pbp::g_PbpDebug = B("PlayerbotsPlus.Debug", false);
    LoadHangout();
    LoadStuckReviveMarks();
    // The packet hook goes by what is published here alone: with the module
    // off, nothing is muted.
    const std::string mute = sConfigMgr->GetOption<std::string>("PlayerbotsPlus.Mute.Patterns", kDefaultMutePatterns);
    PbpMute_Publish(g_Pbp.enable && B("PlayerbotsPlus.Mute.Enable", true), PbpSplit(mute, ';'));

    char const* where = reload ? "module.playerbotsplus" : "server.loading";
    LOG_INFO(where,
             "[Playerbots Plus] {} settings: {}, market {} ({} bot(s) every {}s, again after {}-{} min, sells {}, "
             "buys {})",
             reload ? "Reloaded" : "Loaded", g_Pbp.enable ? "on" : "off", g_Pbp.market.enable ? "on" : "off",
             g_Pbp.market.visitsPerInterval, g_Pbp.market.intervalMs / 1000, g_Pbp.market.cooldownMinMinutes,
             g_Pbp.market.cooldownMaxMinutes, g_Pbp.market.sell ? "yes" : "no", g_Pbp.market.buy ? "yes" : "no");

    uint32_t places = 0;
    for (pbp::PbpHangoutSpot const& spot : pbp::g_HangoutSpots)
        places += spot.capacity;
    LOG_INFO(where,
             "[Playerbots Plus] City hangouts {} ({} spots, room for {}; fetched from elsewhere {}), stuck mounts {}, "
             "getting back up {}, raid marks {} (sapping the star {}).",
             pbp::g_HangoutEnable ? "on" : "off", pbp::g_HangoutSpots.size(), places,
             pbp::g_HangoutBringIn ? "on" : "off", pbp::g_StuckEnable ? "on" : "off",
             pbp::g_ReviveEnable ? "on" : "off", pbp::g_MarksEnable ? "on" : "off",
             pbp::g_MarksSapStar ? "on" : "off");
}
