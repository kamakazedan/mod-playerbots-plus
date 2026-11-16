#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusDungeons.h"
#include "PlayerbotsPlusLedgerText.h"
#include "PlayerbotsPlusMute.h"
#include "PlayerbotsPlusText.h"
#include "PlayerbotsPlusZonesText.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
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
    uint32_t    g_HangoutFullMinutes      = 10;
    uint32_t    g_HangoutMountedChance    = 45;
    uint32_t    g_HangoutBenchChance      = 50;
    uint32_t    g_HangoutSitChance        = 30;
    uint32_t    g_HangoutAfkChance        = 25;
    float       g_HangoutBenchRange       = 25.0f;
    float       g_HangoutHeightTolerance  = 6.0f;
    std::vector<std::string> g_HangoutBenchWords;
    bool        g_HangoutBringIn          = true;
    uint32_t    g_HangoutBringInFill      = 75;
    uint32_t    g_HangoutBringInPerMinute = 15;
    uint32_t    g_HangoutBringInMinLevel  = 10;
    float       g_HangoutArrivalRange     = 400.0f;
    uint32_t    g_HangoutInnArrivalSeconds = 5;
    bool        g_HangoutCityWeights       = true;
    uint32_t    g_HangoutHotShare          = 75;
    std::vector<PbpHangoutZone> g_HangoutZones;
    std::unordered_map<std::string, uint32_t> g_HangoutZonePlaces;
    uint32_t    g_HangoutZoneShiftSeconds  = 30;
    uint32_t    g_HangoutZoneShiftStep     = 3;
    uint32_t    g_HangoutZonePull          = 10;
    uint32_t    g_HangoutZoneMoveSeconds   = 15;
    uint32_t    g_HangoutZoneMovesAtOnce   = 3;

    bool        g_LifeEnable               = true;
    float       g_LifeRange                = 90.0f;
    uint32_t    g_LifeParkedShare          = 15;
    uint32_t    g_LifeRestlessShare        = 25;
    uint32_t    g_LifeShuffleMinSeconds    = 40;
    uint32_t    g_LifeShuffleMaxSeconds    = 120;
    uint32_t    g_LifeRestlessMinSeconds   = 15;
    uint32_t    g_LifeRestlessMaxSeconds   = 30;
    float       g_LifeStepMin              = 2.5f;
    float       g_LifeStepMax              = 7.0f;
    uint32_t    g_LifeWalkChance           = 30;
    uint32_t    g_LifeTurnMinSeconds       = 15;
    uint32_t    g_LifeTurnMaxSeconds       = 60;
    uint32_t    g_LifeHeadTurnChance       = 60;
    uint32_t    g_LifeJumpChance           = 50;
    uint32_t    g_LifeRearChance           = 50;
    uint32_t    g_LifeRearBackChance       = 30;
    bool        g_LifeGawk                 = true;
    uint32_t    g_LifeGawkRareOwners       = 3;
    uint32_t    g_LifeGawkMin              = 2;
    uint32_t    g_LifeGawkMax              = 4;
    float       g_LifeGawkRange            = 30.0f;
    uint32_t    g_LifeGawkMinSeconds       = 20;
    uint32_t    g_LifeGawkMaxSeconds       = 60;
    uint32_t    g_LifeGawkCooldownMinutes  = 15;
    bool        g_LifeDance                = true;
    uint32_t    g_LifeDanceMinutes         = 15;
    uint32_t    g_LifeDanceJoinMin         = 1;
    uint32_t    g_LifeDanceJoinMax         = 4;
    uint32_t    g_LifeDanceMinSeconds      = 60;
    uint32_t    g_LifeDanceMaxSeconds      = 120;
    uint32_t    g_LifeDanceWithChance      = 50;
    uint32_t    g_LifeErrandChance         = 10;
    float       g_LifeErrandRange          = 250.0f;
    uint32_t    g_LifeErrandMinSeconds     = 10;
    uint32_t    g_LifeErrandMaxSeconds     = 40;
    uint32_t    g_LifeErrandStayChance     = 50;
    uint32_t    g_LifeFlyInChance          = 30;
    uint32_t    g_LifeFlyOutChance         = 30;
    bool        g_LifeFish                 = true;
    uint32_t    g_LifeFishChance           = 8;
    uint32_t    g_LifeFishMinMinutes       = 3;
    uint32_t    g_LifeFishMaxMinutes       = 10;
    uint32_t    g_LifeFishPlaces           = 4;
    uint32_t    g_LifeWanderChance         = 15;
    float       g_LifeWanderRange          = 25.0f;
    uint32_t    g_LifeShowOffMinutes       = 15;
    uint32_t    g_LifeShowOffMinSeconds    = 45;
    uint32_t    g_LifeShowOffMaxSeconds    = 75;
    uint32_t    g_LifeShowOffLastChance    = 35;
    PbpCityLife::Temperament g_LifeDefaultTemperament;
    std::unordered_map<std::string, PbpCityLife::Temperament> g_LifeTemperaments;
    std::vector<std::pair<std::string, uint32_t>> g_LifeTemperamentShares;

    bool        g_StuckEnable             = true;
    uint32_t    g_StuckMountCount         = 4;
    uint32_t    g_StuckWindowSeconds      = 30;
    float       g_StuckRadius             = 6.0f;
    uint32_t    g_StuckIgnoreMinutes      = 10;
    uint32_t    g_StuckWalkMinutes        = 10;
    bool        g_FireEnable              = true;
    float       g_FireMargin              = 1.5f;
    bool        g_IndoorsEnable           = true;
    uint32_t    g_IndoorsSeconds          = 3;
    bool        g_SpaceEnable             = true;
    uint32_t    g_SpaceCheckSeconds       = 2;
    uint32_t    g_SpaceStillSeconds       = 10;
    float       g_SpaceDistance           = 1.75f;
    float       g_SpaceStepMin            = 2.5f;
    float       g_SpaceStepMax            = 5.0f;
    uint32_t    g_SpaceCooldownSeconds    = 60;
    float       g_SpaceRange              = 120.0f;
    bool        g_GroundEnable            = true;
    float       g_GroundAirHeight         = 3.0f;
    bool        g_InactiveEnable          = true;
    float       g_InactiveUnseenYards     = 150.0f;

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

    bool        g_PetNamesEnable             = true;
    uint32_t    g_PetNameChance              = 85;
    uint32_t    g_PetNamesPerMinute          = 20;
    std::unordered_map<std::string, std::vector<std::string>> g_PetNames;

    bool        g_CompanionsEnable           = true;
    std::vector<PbpCompanionEntry> g_Companions;
    uint32_t    g_CompanionsOwnChance        = 70;
    uint32_t    g_CompanionsCollectorChance  = 15;
    uint32_t    g_CompanionsCommonWeight     = 4;
    uint32_t    g_CompanionsOutChance        = 30;
    uint32_t    g_CompanionsSwitchChance     = 33;
    uint32_t    g_CompanionsHolidayBoost     = 8;
    uint32_t    g_CompanionsCheckSeconds     = 10;
    float       g_CompanionsRange            = 80.0f;

    bool        g_DuelsEnable                = true;
    std::vector<std::string> g_DuelsSpots;
    uint32_t    g_DuelsCheckSeconds          = 15;
    uint32_t    g_DuelsChance                = 60;
    float       g_DuelsWatchRange            = 150.0f;
    uint32_t    g_DuelsLevelGap              = 5;
    uint32_t    g_DuelsMinLevel              = 10;
    uint32_t    g_DuelsBotCooldownMinutes    = 4;
    uint32_t    g_DuelsMaxAtOnce             = 2;
    uint32_t    g_DuelsChallengePlayerChance  = 0;
    uint32_t    g_DuelsChallengePlayerMinutes = 10;

    bool DuelSpot(std::string const& key)
    {
        if (!g_DuelsEnable)
            return false;
        for (std::string const& spot : g_DuelsSpots)
            if (spot.size() == key.size() &&
                std::equal(spot.begin(), spot.end(), key.begin(), [](char a, char b)
                           { return std::tolower(static_cast<unsigned char>(a)) ==
                                    std::tolower(static_cast<unsigned char>(b)); }))
                return true;
        return false;
    }
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

    // "0,0,0,0.0068": numbers with a fraction, one for each in `out`.
    template <size_t N>
    void ListF(char const* key, std::array<double, N>& out)
    {
        std::string def;
        for (size_t i = 0; i < N; ++i)
        {
            std::ostringstream one;
            one << out[i];
            def += (i ? "," : "") + one.str();
        }
        const std::vector<std::string> parts = PbpSplit(sConfigMgr->GetOption<std::string>(key, def), ',');
        for (size_t i = 0; i < N && i < parts.size(); ++i)
        {
            char* end = nullptr;
            const double v = std::strtod(parts[i].c_str(), &end);
            if (!parts[i].empty() && end && *end == '\0' && v >= 0.0)
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
        { "StormwindTrade",     "0 -8833.4 628.6 94.0 22 16 the Trade District in Stormwind" },
        { "StormwindAuction",   "0 -8815.2 652.9 94.9 14 12 outside the auction house in Stormwind" },
        { "StormwindAuctionHall", "0 -8814.8 666.5 95.5 10 4 the auction house in Stormwind" },
        { "StormwindBank",      "0 -8914.0 621.5 99.6 14 5 the bank in Stormwind" },
        { "StormwindFountain",  "0 -8869.0 643.0 96.4 18 12 by the fountain in the Trade District in Stormwind" },
        { "StormwindValley",    "0 -8955.0 525.0 96.4 20 10 the Valley of Heroes in Stormwind" },
        { "StormwindCathedral", "0 -8635.0 800.0 97.0 24 14 Cathedral Square in Stormwind" },
        { "StormwindMageQuarter", "0 -9010.0 850.0 106.0 18 8 the Mage Quarter in Stormwind" },
        { "StormwindDwarven",   "0 -8425.0 595.0 94.8 18 6 the Dwarven District in Stormwind" },
        { "StormwindOldTown",   "0 -8675.0 445.0 99.5 16 6 Old Town in Stormwind" },
        { "StormwindHarbor",    "0 -8565.5 1195.5 5.4 20 8 the harbor in Stormwind" },
        { "StormwindGates",     "0 -9100.0 380.0 93.8 22 14 outside the gates of Stormwind" },
        { "IronforgeCommons",   "0 -4918.9 -940.4 501.6 22 12 the Commons in Ironforge" },
        { "OrgrimmarValley",    "1 1657.9 -4433.0 17.5 22 14 the Valley of Strength in Orgrimmar" },
        { "OrgrimmarAuctionHall", "1 1678.0 -4450.6 19.3 12 8 the auction house in Orgrimmar" },
        { "OrgrimmarBank",      "1 1615.6 -4391.6 10.3 14 8 outside the bank in Orgrimmar" },
        { "OrgrimmarCourtyard", "1 1578.0 -4412.0 8.5 30 18 the middle of the Valley of Strength in Orgrimmar" },
        { "OrgrimmarGateway",   "1 1527.0 -4408.0 18.0 22 10 just inside the gates of Orgrimmar" },
        { "OrgrimmarSpirits",   "1 1566.2 -4194.1 42.9 22 10 the crossroads by the Valley of Spirits in Orgrimmar" },
        { "OrgrimmarWisdom",    "1 1958.0 -4330.0 22.3 20 8 the Valley of Wisdom in Orgrimmar" },
        { "OrgrimmarWestRoad",  "1 1666.0 -4100.0 35.5 18 8 the road west of the Hall of Legends in Orgrimmar" },
        { "OrgrimmarCleft",     "1 1768.0 -4320.9 6.6 16 8 the mouth of the Cleft of Shadow in Orgrimmar" },
        { "OrgrimmarCleftStone",
          "1 1810.0 -4389.0 -17.5 14 4 the meeting stone deep in the Cleft of Shadow in Orgrimmar" },
        { "OrgrimmarCleftDepths",
          "1 1840.0 -4333.0 -15.2 12 3 the warlock trainers deep in the Cleft of Shadow in Orgrimmar" },
        { "OrgrimmarHonor",     "1 2076.4 -4824.2 23.7 18 8 the Valley of Honor in Orgrimmar" },
        { "OrgrimmarGates",     "1 1320.0 -4400.0 27.0 22 14 outside the gates of Orgrimmar" },
        { "OrgrimmarZeppelins", "1 1342.0 -4645.0 53.6 10 5 the zeppelin tower outside Orgrimmar" },
        { "UndercityTrade",     "0 1584.1 240.3 -52.2 16 10 the Trade Quarter in the Undercity" },
        { "ThunderBluffMiddle", "1 -1277.4 124.8 131.3 30 16 the middle of Thunder Bluff" },
        { "ThunderBluffBank",   "1 -1263.3 44.5 127.5 16 8 outside the bank in Thunder Bluff" },
        { "DarnassusBank",      "1 9943.0 2497.7 1317.7 16 8 outside the bank in Darnassus" },
        { "ExodarHall",         "530 -3965.7 -11653.6 -138.8 18 8 the main hall of the Exodar" },
        { "SilvermoonExchange", "530 9744.0 -7466.1 13.5 22 10 the Royal Exchange in Silvermoon" },
        { "ShattrathTerrace",   "530 -1863.7 5430.0 -9.6 24 12 the Terrace of Light in Shattrath" },
        { "DalaranBank",        "571 5958.1 617.3 650.6 18 14 outside the bank in Dalaran" },
        { "DalaranRuneweaver",  "571 5796.9 646.1 647.4 20 16 Runeweaver Square in Dalaran" },
        { "DalaranFountain",    "571 5704.2 615.7 646.8 16 12 the fountain in the Eventide in Dalaran" },
        { "DalaranKrasus",      "571 5813.4 453.4 658.8 16 10 Krasus' Landing in Dalaran" },
        { "DalaranMerchants",   "571 5654.9 682.2 652.0 14 10 outside the Merchants' Bank in Dalaran" },
    };

    // How the kinds of people about a city spend their time there
    // (CityLife.Temperament.<Name>): changes to the default, which is the
    // CityLife.* and Hangout.* settings. A chat module says which a bot is,
    // by its personality; without one, TemperamentShares decides.
    const std::pair<const char*, const char*> kDefaultTemperaments[] = {
        { "Calm",   "parked:50|restless:0|jump:15|rear:15|wander:0|bench:75|sit:50|afk:45|mounted:25" },
        { "Lively", "parked:5|restless:45|jump:60|rear:60|wander:30|bench:20|sit:10|afk:5|mounted:55" },
        { "Clown",  "parked:0|restless:60|jump:70|rear:75|wander:30|bench:5|sit:5|afk:0|mounted:80|showoff:70" },
    };
    char const* const kDefaultTemperamentShares = "calm:25|lively:20|clown:5";

    // Each city's busiest places, where most of its people are: the Trade
    // District, the Commons, the Valley of Strength, the middle of Thunder
    // Bluff, the banks and auction houses.
    char const* const kDefaultHotSpots =
        "StormwindTrade|StormwindAuction|StormwindAuctionHall|StormwindBank|StormwindFountain|IronforgeCommons|"
        "OrgrimmarValley|OrgrimmarAuctionHall|OrgrimmarBank|OrgrimmarCourtyard|OrgrimmarGateway|UndercityTrade|"
        "ThunderBluffMiddle|ThunderBluffBank|DarnassusBank|ExodarHall|SilvermoonExchange|ShattrathTerrace|"
        "DalaranBank|DalaranRuneweaver|DalaranMerchants";

    // A city's zones (Hangout.Zone.<City>.<Name>): "tier spot spot ...", the
    // spots sharing what the zone's share of the city's people comes to.
    // Used with the spots above (the conf has no Hangout.Spot.* keys) when
    // the conf has no Hangout.Zone.* keys either. Orgrimmar's: most people
    // in the Valley of Strength, some at the crossroads by the Valley of
    // Spirits, a few in each of the Valley of Wisdom, the Cleft of Shadow,
    // the Valley of Honor and the road west of the Hall of Legends.
    const std::pair<const char*, const char*> kDefaultHangoutZones[] = {
        { "Orgrimmar.Strength",
          "high OrgrimmarValley OrgrimmarAuctionHall OrgrimmarBank OrgrimmarCourtyard OrgrimmarGateway" },
        { "Orgrimmar.Spirits", "medium OrgrimmarSpirits" },
        { "Orgrimmar.Wisdom", "low OrgrimmarWisdom" },
        { "Orgrimmar.Cleft", "low OrgrimmarCleft OrgrimmarCleftStone OrgrimmarCleftDepths" },
        { "Orgrimmar.Honor", "low OrgrimmarHonor" },
        { "Orgrimmar.WestRoad", "low OrgrimmarWestRoad" },
    };

    // What a zone of each tier holds of its city's people (Hangout.ZoneTier.<Name>):
    // "least start most", in percent.
    const std::pair<const char*, const char*> kDefaultZoneTiers[] = {
        { "Low", "1 5 10" },
        { "Medium", "10 15 20" },
        { "High", "50 65 80" },
    };

    // The city weights playerbots sends its bots to the cities' bankers by,
    // read as it reads them (its keys and defaults), by the city a spot's name
    // says it is in (PbpCityOf).
    std::map<std::string, uint32_t> PlayerbotsCityWeights()
    {
        auto w = [](char const* key, int def)
        {
            return uint32_t(std::max(0, sConfigMgr->GetOption<int>(key, def, false)));
        };
        return {
            { "Stormwind", w("Playerbots.TeleToStormwindWeight", 2) },
            { "Ironforge", w("Playerbots.TeleToIronforgeWeight", 1) },
            { "Darnassus", w("Playerbots.TeleToDarnassusWeight", 1) },
            { "Exodar", w("Playerbots.TeleToExodarWeight", 1) },
            { "Orgrimmar", w("Playerbots.TeleToOrgrimmarWeight", 2) },
            { "Undercity", w("Playerbots.TeleToUndercityWeight", 1) },
            { "Thunder Bluff", w("Playerbots.TeleToThunderBluffWeight", 1) },
            { "Silvermoon", w("Playerbots.TeleToSilvermoonCityWeight", 1) },
            { "Shattrath", w("Playerbots.TeleToShattrathCityWeight", 1) },
            { "Dalaran", w("Playerbots.TeleToDalaranWeight", 1) },
        };
    }

    void LoadCityLife()
    {
        using namespace pbp;
        auto range = [](char const* lo, char const* hi, uint32_t defLo, uint32_t defHi, uint32_t min, uint32_t max,
                        uint32_t& outLo, uint32_t& outHi)
        {
            outLo = std::clamp<uint32_t>(U(lo, defLo), min, max);
            outHi = std::clamp<uint32_t>(U(hi, defHi), outLo, max);
        };
        auto pct = [](char const* key, uint32_t def) { return std::min<uint32_t>(100, U(key, def)); };
        g_LifeEnable         = B("PlayerbotsPlus.CityLife.Enable", true);
        g_LifeRange          = std::clamp(F("PlayerbotsPlus.CityLife.Range", 90.0f), 20.0f, 250.0f);
        g_LifeParkedShare    = pct("PlayerbotsPlus.CityLife.ParkedShare", 15);
        g_LifeRestlessShare  = pct("PlayerbotsPlus.CityLife.RestlessShare", 25);
        range("PlayerbotsPlus.CityLife.ShuffleMinSeconds", "PlayerbotsPlus.CityLife.ShuffleMaxSeconds", 40, 120, 5,
              3600, g_LifeShuffleMinSeconds, g_LifeShuffleMaxSeconds);
        range("PlayerbotsPlus.CityLife.RestlessMinSeconds", "PlayerbotsPlus.CityLife.RestlessMaxSeconds", 15, 30, 5,
              3600, g_LifeRestlessMinSeconds, g_LifeRestlessMaxSeconds);
        g_LifeStepMin        = std::clamp(F("PlayerbotsPlus.CityLife.StepMin", 2.5f), 1.0f, 20.0f);
        g_LifeStepMax        = std::clamp(F("PlayerbotsPlus.CityLife.StepMax", 7.0f), g_LifeStepMin, 30.0f);
        g_LifeWalkChance     = pct("PlayerbotsPlus.CityLife.WalkChance", 30);
        range("PlayerbotsPlus.CityLife.TurnMinSeconds", "PlayerbotsPlus.CityLife.TurnMaxSeconds", 15, 60, 3, 3600,
              g_LifeTurnMinSeconds, g_LifeTurnMaxSeconds);
        g_LifeHeadTurnChance = pct("PlayerbotsPlus.CityLife.HeadTurnChance", 60);
        g_LifeJumpChance     = pct("PlayerbotsPlus.CityLife.JumpChance", 50);
        g_LifeRearChance     = pct("PlayerbotsPlus.CityLife.RearChance", 50);
        g_LifeRearBackChance = pct("PlayerbotsPlus.CityLife.RearBackChance", 30);
        g_LifeGawk           = B("PlayerbotsPlus.CityLife.Gawk", true);
        g_LifeGawkRareOwners = std::min<uint32_t>(1000, U("PlayerbotsPlus.CityLife.GawkRareOwners", 3));
        range("PlayerbotsPlus.CityLife.GawkMin", "PlayerbotsPlus.CityLife.GawkMax", 2, 4, 1, 8, g_LifeGawkMin,
              g_LifeGawkMax);
        g_LifeGawkRange      = std::clamp(F("PlayerbotsPlus.CityLife.GawkRange", 30.0f), 5.0f, 80.0f);
        range("PlayerbotsPlus.CityLife.GawkMinSeconds", "PlayerbotsPlus.CityLife.GawkMaxSeconds", 20, 60, 5, 600,
              g_LifeGawkMinSeconds, g_LifeGawkMaxSeconds);
        g_LifeGawkCooldownMinutes = std::min<uint32_t>(1440, U("PlayerbotsPlus.CityLife.GawkCooldownMinutes", 15));
        g_LifeDance          = B("PlayerbotsPlus.CityLife.Dance", true);
        g_LifeDanceMinutes   = std::clamp<uint32_t>(U("PlayerbotsPlus.CityLife.DanceMinutes", 15), 1, 1440);
        range("PlayerbotsPlus.CityLife.DanceJoinMin", "PlayerbotsPlus.CityLife.DanceJoinMax", 1, 4, 0, 10,
              g_LifeDanceJoinMin, g_LifeDanceJoinMax);
        range("PlayerbotsPlus.CityLife.DanceMinSeconds", "PlayerbotsPlus.CityLife.DanceMaxSeconds", 60, 120, 10, 900,
              g_LifeDanceMinSeconds, g_LifeDanceMaxSeconds);
        g_LifeDanceWithChance = pct("PlayerbotsPlus.CityLife.DanceWithChance", 50);
        g_LifeErrandChance   = pct("PlayerbotsPlus.CityLife.ErrandChance", 10);
        g_LifeErrandRange    = std::clamp(F("PlayerbotsPlus.CityLife.ErrandRange", 250.0f), 20.0f, 600.0f);
        range("PlayerbotsPlus.CityLife.ErrandMinSeconds", "PlayerbotsPlus.CityLife.ErrandMaxSeconds", 10, 40, 3, 600,
              g_LifeErrandMinSeconds, g_LifeErrandMaxSeconds);
        g_LifeErrandStayChance = pct("PlayerbotsPlus.CityLife.ErrandStayChance", 50);
        g_LifeFlyInChance    = pct("PlayerbotsPlus.CityLife.FlyInChance", 30);
        g_LifeFlyOutChance   = pct("PlayerbotsPlus.CityLife.FlyOutChance", 30);
        g_LifeFish           = B("PlayerbotsPlus.CityLife.Fish", true);
        g_LifeFishChance     = pct("PlayerbotsPlus.CityLife.FishChance", 8);
        range("PlayerbotsPlus.CityLife.FishMinMinutes", "PlayerbotsPlus.CityLife.FishMaxMinutes", 3, 10, 1, 60,
              g_LifeFishMinMinutes, g_LifeFishMaxMinutes);
        g_LifeFishPlaces     = std::min<uint32_t>(20, U("PlayerbotsPlus.CityLife.FishPlaces", 4));
        g_LifeWanderChance   = pct("PlayerbotsPlus.CityLife.WanderChance", 15);
        g_LifeWanderRange    = std::clamp(F("PlayerbotsPlus.CityLife.WanderRange", 25.0f), 8.0f, 60.0f);
        g_LifeShowOffMinutes = std::clamp<uint32_t>(U("PlayerbotsPlus.CityLife.ShowOffMinutes", 15), 1, 1440);
        range("PlayerbotsPlus.CityLife.ShowOffMinSeconds", "PlayerbotsPlus.CityLife.ShowOffMaxSeconds", 45, 75, 10,
              600, g_LifeShowOffMinSeconds, g_LifeShowOffMaxSeconds);
        g_LifeShowOffLastChance = pct("PlayerbotsPlus.CityLife.ShowOffLastChance", 35);

        // Temperaments: the default is the settings above and the hangout's
        // own (read before this); each named one changes some of it.
        PbpCityLife::Temperament& base = g_LifeDefaultTemperament;
        base          = PbpCityLife::Temperament();
        base.parked   = g_LifeParkedShare;
        base.restless = g_LifeRestlessShare;
        base.jump     = g_LifeJumpChance;
        base.rear     = g_LifeRearChance;
        base.wander   = g_LifeWanderChance;
        base.showOff  = 0;
        base.bench    = g_HangoutBenchChance;
        base.mounted  = g_HangoutMountedChance;
        base.sit      = g_HangoutSitChance;
        base.afk      = g_HangoutAfkChance;
        g_LifeTemperaments.clear();
        std::string bad;
        auto addTemperament = [&bad](std::string const& name, std::string const& text)
        {
            std::string wrong;
            const std::string key = PbpCityLife::TemperamentKey(name);
            g_LifeTemperaments[key] = PbpCityLife::ParseTemperament(text, g_LifeDefaultTemperament, wrong);
            if (!wrong.empty())
                bad += (bad.empty() ? "" : "; ") + name + ": " + wrong;
        };
        const std::string prefix = "PlayerbotsPlus.CityLife.Temperament.";
        const std::vector<std::string> keys = sConfigMgr->GetKeysByString(prefix);
        if (keys.empty())
            for (auto const& [name, text] : kDefaultTemperaments)
                addTemperament(name, text);
        for (std::string const& key : keys)
            addTemperament(key.substr(prefix.size()), sConfigMgr->GetOption<std::string>(key, ""));
        g_LifeTemperamentShares = PbpCityLife::ParseShares(
            sConfigMgr->GetOption<std::string>("PlayerbotsPlus.CityLife.TemperamentShares", kDefaultTemperamentShares),
            bad);
        for (auto const& [name, share] : g_LifeTemperamentShares)
            if (!g_LifeTemperaments.contains(name) && name != "default")
                bad += (bad.empty() ? "" : "; ") + std::string("TemperamentShares: no temperament '") + name + "'";
        if (!bad.empty())
            LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.CityLife: could not read {} (want "
                      "\"parked:N|restless:N|jump:N|rear:N|wander:N|showoff:N|bench:N|mounted:N|sit:N|afk:N\", each 0 "
                      "to 100).", bad);
    }

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
        g_HangoutFullMinutes     = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.FullMinutes", 10), 1, 120);
        g_HangoutMountedChance   = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.MountedChance", 45));
        g_HangoutBenchChance     = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.BenchChance", 50));
        g_HangoutSitChance       = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.SitChance", 30));
        g_HangoutAfkChance       = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.AfkChance", 25));
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
        g_HangoutInnArrivalSeconds = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.InnArrivalSeconds", 5), 1, 60);

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
            spot.written  = line.capacity;
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

        // Most of a city's people at its busiest places (a city with zones
        // goes by those instead: LoadHangoutZones).
        const std::vector<std::string> hot =
            PbpSplit(sConfigMgr->GetOption<std::string>("PlayerbotsPlus.Hangout.HotSpots", kDefaultHotSpots), '|');
        for (PbpHangoutSpot& spot : g_HangoutSpots)
            spot.hot = std::find(hot.begin(), hot.end(), spot.key) != hot.end();
        g_HangoutCityWeights = B("PlayerbotsPlus.Hangout.CityWeights", true);
        g_HangoutHotShare    = std::min<uint32_t>(100, U("PlayerbotsPlus.Hangout.HotShare", 75));
        // Shared out among the spots once the zones are read (LoadHangoutZones).
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

        g_FireEnable = B("PlayerbotsPlus.Fire.Enable", true);
        g_FireMargin = std::clamp(F("PlayerbotsPlus.Fire.Margin", 1.5f), 0.5f, 5.0f);

        g_IndoorsEnable  = B("PlayerbotsPlus.Indoors.Enable", true);
        g_IndoorsSeconds = std::clamp<uint32_t>(U("PlayerbotsPlus.Indoors.Seconds", 3), 1, 60);

        g_SpaceEnable          = B("PlayerbotsPlus.Space.Enable", true);
        g_SpaceCheckSeconds    = std::clamp<uint32_t>(U("PlayerbotsPlus.Space.CheckSeconds", 2), 1, 30);
        g_SpaceStillSeconds    = std::min<uint32_t>(U("PlayerbotsPlus.Space.StillSeconds", 10), 3600);
        g_SpaceDistance        = std::clamp(F("PlayerbotsPlus.Space.Distance", 1.75f), 0.5f, 5.0f);
        g_SpaceStepMin         = std::clamp(F("PlayerbotsPlus.Space.StepMin", 2.5f), 1.0f, 10.0f);
        g_SpaceStepMax         = std::clamp(F("PlayerbotsPlus.Space.StepMax", 5.0f), g_SpaceStepMin, 15.0f);
        g_SpaceCooldownSeconds = std::min<uint32_t>(U("PlayerbotsPlus.Space.CooldownSeconds", 60), 3600);
        g_SpaceRange           = std::clamp(F("PlayerbotsPlus.Space.Range", 120.0f), 0.0f, 1000.0f);

        g_GroundEnable    = B("PlayerbotsPlus.Ground.Enable", true);
        g_GroundAirHeight = std::clamp(F("PlayerbotsPlus.Ground.AirHeight", 3.0f), 2.0f, 20.0f);

        g_InactiveEnable      = B("PlayerbotsPlus.Inactive.Enable", true);
        g_InactiveUnseenYards = std::clamp(F("PlayerbotsPlus.Inactive.UnseenYards", 150.0f), 50.0f, 1000.0f);

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

namespace
{
    // What hunters call their pets, by family (PlayerbotsPlus.Pets.Names.<Family>),
    // and what any pet may be called (.Any). Used when the conf has no
    // PlayerbotsPlus.Pets.Names.* keys.
    const std::pair<const char*, const char*> kDefaultPetNames[] = {
        { "Any",         "Fluffy|Mittens|Snuggles|Biscuit|Pickles|Nugget|Waffles|Muffin|Noodle|Pudding|Gizmo|Chewie|"
                         "Buddy|Rocky|Tank|Bruno|Kevin|Steve|Gary|Dave|Barry|Nigel|Trevor|Colin|Keith|Derek|Geoff|"
                         "Clive|Bob|Doug|Princess|Tiny|Chonk|Pebbles" },
        { "Wolf",        "Ghost|Nymeria|Greywind|Fenrir|Balto|Akela|Lupin|Sirius|Fang|Scooby|Snoopy|Gromit|Goodboy|"
                         "Barkley|Bigbad|Wolfgang|Howler|Lassie|Beethoven|Hachiko" },
        { "Cat",         "Aslan|Simba|Nala|Mufasa|Bagheera|Sherekhan|Garfield|Salem|Tigger|Hobbes|Crookshanks|"
                         "Bigglesworth|Tibbles|Whiskers|Purrcy|Clawdia|Catniss|Felix|Smudge|Jinx" },
        { "Spider",      "Aragog|Shelob|Charlotte|Itsy|Bitsy|Webster|Spidey|Parker|Legsy|Tickles|Nopenope|Eightball" },
        { "Bear",        "Baloo|Paddington|Pooh|Yogi|Booboo|Fozzie|Iorek|Smokey|Rupert|Bungle|Winnie|Barnaby|Teddy|"
                         "Grizzwald" },
        { "Boar",        "Pumbaa|Babe|Wilbur|Hamlet|Porkchop|Bacon|Truffles|Napoleon|Piglet|Peppa|Hogwart|Sausage|"
                         "Gammon|Squealer|Kevin" },
        { "Crocolisk",   "Tick|Tock|Snappy|Lyle|Dundee|Sobek|Chompers|Handbag|Smiley|Crunch|Gator|Wally" },
        { "CarrionBird", "Nevermore|Quoth|Edgar|Iago|Zazu|Scuttle|Beaky|Plucky|Crowley|Pecky|Vulcan" },
        { "Crab",        "Sebastian|Krabs|Pinchy|Crabby|Snappy|Larry|Clawdius|Itchy|Clawde|Sidestep|Shelly" },
        { "Gorilla",     "Kong|Caesar|Cornelius|Koko|Bubbles|Bananas|Mojo|Magilla|Donkey|Bigfoot|Grape" },
        { "Raptor",      "Blue|Rexy|Clever|Littlefoot|Barney|Yoshi|Chompy|Velma|Raptorella|Ducky|Spike|Dino" },
        { "Tallstrider", "Bigbird|Roadrunner|Ozzy|Stilts|Legs|Gonzo|Strider|Dodo|Tweety|Lanky" },
        { "Scorpid",     "Pinchy|Stinger|Scorpio|Sting|Snippy|Pinchers|Tailspin|Zappa|Kevin|Stingo" },
        { "Turtle",      "Leonardo|Raphael|Donatello|Michelangelo|Crush|Squirt|Shelly|Speedy|Morla|Koopa|Bowser|"
                         "Franklin|Sheldon|Turbo" },
        { "Bat",         "Stellaluna|Nosferatu|Dracula|Vlad|Bruce|Alfred|Robin|Batty|Wayne|Count|Echo|Fang" },
        { "Hyena",       "Shenzi|Banzai|Ed|Giggles|Cackles|Chuckles|Snickers|Hehe|Scraps|Mangy" },
        { "Owl",         "Hedwig|Errol|Archimedes|Hootie|Hoot|Owlbert|Hootsalot|Fawkes|Buckbeak|Talon|Kevin" },
        { "WindSerpent", "Falkor|Haku|Mushu|Shenron|Puff|Noodle|Toothless|Drogon|Spyro|Dangernoodle|Zephyr|Kaa" },
        { "Dragonhawk",  "Puff|Smaug|Norbert|Drogon|Rhaegal|Viserion|Toothless|Spyro|Saphira|Elliott|Ember|Sparky" },
        { "Ravager",     "Spike|Gnasher|Chomper|Mangler|Pokey|Snips|Pincushion|Kevin|Rex|Shredder" },
        { "WarpStalker", "Blinky|Ghost|Houdini|Shadow|Flicker|Peekaboo|Kurt|Blink|Phase|Poof" },
        { "Sporebat",    "Shroom|Spore|Toadstool|Morel|Portobello|Funguy|Mushy|Puffball|Truffle|Floaty" },
        { "NetherRay",   "Manta|Raymond|Ray|Stingray|Flappy|Rayray|Rayman|Skate|Sheila|Wobbles" },
        { "Serpent",     "Kaa|Nagini|Hiss|Slinky|Monty|Noodle|Snek|Dangernoodle|Sirhiss|Basil|Slither" },
        { "Moth",        "Mothra|Mothy|Flutter|Lumos|Dusty|Moffy|Lampy|Fuzzy|Nightlight|Lamp" },
        { "Chimaera",    "Hydra|Cerberus|Fluffy|Ghidorah|Chimchim|Twoface|Gemini|Twins" },
        { "Devilsaur",   "Rex|Godzilla|Rexy|Chomper|Tiny|Sharptooth|Barney|Littlefoot|Princess|Dino|Stompy" },
        { "Silithid",    "Bug|Buggy|Klendathu|Antz|Skitter|Zerg|Crunchy|Kafka|Chitter|Kevin" },
        { "Worm",        "Graboid|Shaihulud|Wiggles|Squiggles|Jim|Wormtail|Slurms|Noodle|Wormy|Dune" },
        { "Rhino",       "Rocksteady|Bebop|Bruiser|Tank|Rhonda|Kevin|Thunder|Stompy|Horace" },
        { "Wasp",        "Buzz|Buzzy|Stingy|Bumble|Waspy|Beeyonce|Zippy|Hornet|Bzzt" },
        { "CoreHound",   "Cerberus|Fluffy|Hotdog|Ember|Cinder|Sparky|Magma|Scorch|Toasty|Lava" },
        { "SpiritBeast", "Ghost|Spirit|Casper|Phantom|Spectre|Boo|Wisp|Shimmer|Aslan|Moonlight" },
    };

    // Companions by side, "item:minimum level:weight" (weight 10 and level 1
    // when left out): the pet sellers' wares commonest, then drops, quest
    // rewards, holidays and the rare and promotional ones.
    // The pet sellers' wares (Elwynn's cats, Darnassus' owls, the Exodar's
    // moths; Orgrimmar's snakes, the Undercity's roach, Thunder Bluff's
    // prairie dog, Eversong's dragonhawks) at 10, when a few silver to spare
    // is likely; the Argent Tournament's at 80.
    constexpr char const* kDefaultCompanionsAlliance =
        "8485:10|8486:10|8487:10|8488:10|8489:10:8|8497:8:8|8500:10:8|8501:10:8|29901:10:8|29903:10:8|"
        "29904:10:8|49662:1:1|44998:80:2|44965:80:2|44970:80:2|44974:80:2|44984:80:2|45002:80:2|46820:80:1";
    constexpr char const* kDefaultCompanionsHorde =
        "10360:10|10361:10|10392:10|10393:10:8|10394:10:8|29953:10:8|29956:10:8|29957:10:8|49663:1:1|"
        "45022:80:2|44971:80:2|44973:80:2|44980:80:2|44982:80:2|45606:80:2|46821:80:1";
    // Each at the level where it is had: the Darkmoon Faire's frogs, Booty
    // Bay's parrots at 30, the Stormspire's at 65, Dalaran's pet shop at 70;
    // engineers' toys; drops where they drop (the Deadmines' macaw at 18,
    // Wetlands whelps at 22, Winterspring's at 55); the holidays' own, tagged
    // with the holiday; and the store and collector's edition ones, anywhere.
    constexpr char const* kDefaultCompanionsBoth =
        "11026:10:4:darkmoon|11027:10:4:darkmoon|11110:12:2|11023:25:4|8495:30:4|8496:30:4|8490:65:4|"
        "29363:65:4|29364:65:4|29902:65:4|29958:65:4|44822:70:4|46398:70:4|48120:70:3|39973:70:2|54436:70:2|"
        "4401:15:2|11825:41:1|11826:41:1|15996:53:1|21277:55:1|48114:17:1|8492:18:2|48118:18:1|8499:22:2|"
        "10822:25:2|8491:25:2|48124:28:1|48112:35:1|8494:40:1|18964:40:1|10398:45:2|11474:45:2|8498:45:2|"
        "19450:45:2:darkmoon|48122:50:1|20769:50:2|34535:55:2|48126:60:1|29960:62:2|34478:64:1|27445:65:1|"
        "33816:70:1|33818:70:1|38628:70:1|35504:70:1|33993:70:1|38658:70:1|35349:74:1|35350:74:1|43698:74:1|"
        "44983:74:2|44723:74:1|44738:75:1|39896:76:1|39899:76:1|39898:76:1|44721:76:1|48116:76:1|49912:15:1|"
        "23002:10:2:childrensweek|23007:10:2:childrensweek|23015:10:2:childrensweek|32616:60:1:childrensweek|"
        "32617:60:1:childrensweek|32622:60:1:childrensweek|46544:70:1:childrensweek|46545:70:1:childrensweek|"
        "21301:10:2:winterveil|21305:10:2:winterveil|21308:10:2:winterveil|21309:10:2:winterveil|"
        "34425:10:1:winterveil|33154:10:2:hallowsend|23083:10:1:midsummer|34955:10:2:midsummer|"
        "53641:75:1:midsummer|22235:10:2:loveisintheair|50446:78:1:loveisintheair|44794:10:2:noblegarden|"
        "44810:10:2:pilgrimsbounty|32233:10:2:brewfest|46707:10:1:brewfest|49665:1:1|49693:1:1|54847:1:1|"
        "13582:1:1|13583:1:1|13584:1:1|25535:1:1|39286:1:1|49343:1:1";

    // Where people duel: just outside the main gates of Orgrimmar and
    // Stormwind, as on any realm.
    constexpr char const* kDefaultDuelSpots = "OrgrimmarGates|StormwindGates";

    // Bot dungeons by level: one run in each band from the first dungeons
    // to Outland, the rest at the level cap's end.
    constexpr char const* kDefaultLevelBands = "15-19,20-29,30-39,40-49,50-59,60-69,70-80";
    constexpr char const* kDefaultLevelBandRuns = "1,1,1,1,1,1,4";

    // "graboid" -> "Graboid": a pet name the way the game keeps one. Empty
    // for one the game would not take: letters only, 2 to 12 of them.
    std::string PetName(std::string name)
    {
        if (name.size() < 2 || name.size() > 12 ||
            !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalpha(c) != 0; }))
            return {};
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        return name;
    }

    std::string Lower(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    void LoadPetsDuels()
    {
        using namespace pbp;
        g_PetNamesEnable    = B("PlayerbotsPlus.Pets.NamesEnable", true);
        g_PetNameChance     = std::min<uint32_t>(100, U("PlayerbotsPlus.Pets.NameChance", 85));
        g_PetNamesPerMinute = std::clamp<uint32_t>(U("PlayerbotsPlus.Pets.NamesPerMinute", 20), 1, 600);

        g_PetNames.clear();
        std::string bad;
        auto addNames = [&bad](std::string const& family, std::string const& value)
        {
            std::vector<std::string>& names = g_PetNames[Lower(family)];
            for (std::string const& raw : PbpSplit(value, '|'))
            {
                const std::string name = PetName(raw);
                if (name.empty())
                    bad += (bad.empty() ? "" : ", ") + raw;
                else
                    names.push_back(name);
            }
        };
        const std::string namesPrefix = "PlayerbotsPlus.Pets.Names.";
        const std::vector<std::string> nameKeys = sConfigMgr->GetKeysByString(namesPrefix);
        if (nameKeys.empty())
            for (auto const& [family, names] : kDefaultPetNames)
                addNames(family, names);
        for (std::string const& key : nameKeys)
            addNames(key.substr(namesPrefix.size()), sConfigMgr->GetOption<std::string>(key, ""));
        if (!bad.empty())
            LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.Pets.Names: left out {} (a pet name is "
                      "2 to 12 letters, nothing else).", bad);

        g_CompanionsEnable          = B("PlayerbotsPlus.Companions.Enable", true);
        g_CompanionsOwnChance       = std::min<uint32_t>(100, U("PlayerbotsPlus.Companions.OwnChance", 70));
        g_CompanionsCollectorChance = std::min<uint32_t>(100, U("PlayerbotsPlus.Companions.CollectorChance", 15));
        g_CompanionsCommonWeight    = U("PlayerbotsPlus.Companions.CommonWeight", 4);
        g_CompanionsOutChance       = std::min<uint32_t>(100, U("PlayerbotsPlus.Companions.OutChance", 30));
        g_CompanionsSwitchChance    = std::min<uint32_t>(100, U("PlayerbotsPlus.Companions.SwitchChance", 33));
        g_CompanionsHolidayBoost    = std::clamp<uint32_t>(U("PlayerbotsPlus.Companions.HolidayBoost", 8), 1, 100);
        g_CompanionsCheckSeconds    = std::clamp<uint32_t>(U("PlayerbotsPlus.Companions.CheckSeconds", 10), 2, 300);
        g_CompanionsRange           = std::clamp(F("PlayerbotsPlus.Companions.Range", 80.0f), 10.0f, 200.0f);
        g_Companions.clear();
        std::string badCompanions;
        auto addCompanions = [&badCompanions](char const* key, char const* def, uint8_t side)
        {
            for (std::string const& part : PbpSplit(sConfigMgr->GetOption<std::string>(key, def), '|'))
            {
                const std::vector<std::string> f = PbpSplit(part, ':');
                PbpCompanionEntry e;
                e.side = side;
                if (f.size() > 3)
                    e.holiday = PbpHolidayKey(f[3]);
                if (f.empty() || f.size() > 4 || !ToUInt(f[0], e.item) || !e.item ||
                    (f.size() > 1 && !ToUInt(f[1], e.minLevel)) || (f.size() > 2 && !ToUInt(f[2], e.weight)) ||
                    !e.weight || (f.size() > 3 && !e.holiday))
                {
                    badCompanions += (badCompanions.empty() ? "" : ", ") + part;
                    continue;
                }
                g_Companions.push_back(e);
            }
        };
        addCompanions("PlayerbotsPlus.Companions.Alliance", kDefaultCompanionsAlliance, 1);
        addCompanions("PlayerbotsPlus.Companions.Horde", kDefaultCompanionsHorde, 2);
        addCompanions("PlayerbotsPlus.Companions.Both", kDefaultCompanionsBoth, 0);
        if (!badCompanions.empty())
            LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.Companions: could not read {} (want "
                      "item, item:level, item:level:weight or item:level:weight:holiday).", badCompanions);

        g_DuelsEnable             = B("PlayerbotsPlus.Duels.Enable", true);
        g_DuelsSpots              = PbpSplit(sConfigMgr->GetOption<std::string>("PlayerbotsPlus.Duels.Spots",
                                                                                kDefaultDuelSpots), '|');
        g_DuelsCheckSeconds       = std::clamp<uint32_t>(U("PlayerbotsPlus.Duels.CheckSeconds", 15), 5, 600);
        g_DuelsChance             = std::min<uint32_t>(100, U("PlayerbotsPlus.Duels.Chance", 60));
        g_DuelsWatchRange         = std::clamp(F("PlayerbotsPlus.Duels.WatchRange", 150.0f), 20.0f, 500.0f);
        g_DuelsLevelGap           = std::min<uint32_t>(79, U("PlayerbotsPlus.Duels.LevelGap", 5));
        g_DuelsMinLevel           = std::clamp<uint32_t>(U("PlayerbotsPlus.Duels.MinLevel", 10), 1, 80);
        g_DuelsBotCooldownMinutes = U("PlayerbotsPlus.Duels.BotCooldownMinutes", 4);
        g_DuelsMaxAtOnce          = std::clamp<uint32_t>(U("PlayerbotsPlus.Duels.MaxAtOnce", 2), 1, 10);
        g_DuelsChallengePlayerChance  = std::min<uint32_t>(100, U("PlayerbotsPlus.Duels.ChallengePlayerChance", 0));
        g_DuelsChallengePlayerMinutes = std::max<uint32_t>(1, U("PlayerbotsPlus.Duels.ChallengePlayerMinutes", 10));

        // Each one has to be a hangout spot: that is where the bots gather.
        std::string unknown;
        for (std::string const& key : g_DuelsSpots)
            if (std::none_of(g_HangoutSpots.begin(), g_HangoutSpots.end(),
                             [&key](PbpHangoutSpot const& s) { return Lower(s.key) == Lower(key); }))
                unknown += (unknown.empty() ? "" : ", ") + key;
        if (!unknown.empty())
            LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.Duels.Spots: no hangout spot called {} "
                      "(PlayerbotsPlus.Hangout.Spot.<Name>).", unknown);
    }
}

namespace
{
    // A city's zones (Hangout.Zone.*), then every spot's share of the
    // places: each city as busy as playerbots' weights for it say
    // (Hangout.CityWeights), the written capacities only each spot's part
    // against the others of its kind in its city -- most of its people at
    // its hot spots (Hangout.HotShare). A city with zones goes by its zones
    // instead: its spots outside them (where duels are fought, the zeppelin
    // tower) keep the people written for them, and the rest of its places
    // go to its zones by their shares (PbpZones, drifting as the server
    // runs: PlayerbotsPlusHangout). After LoadPetsDuels, which says where
    // duels are fought: such a spot is never in a zone.
    void LoadHangoutZones()
    {
        using namespace pbp;
        g_HangoutZoneShiftSeconds = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.ZoneShiftSeconds", 30), 5, 3600);
        g_HangoutZoneShiftStep    = std::min<uint32_t>(U("PlayerbotsPlus.Hangout.ZoneShiftStep", 3), 20);
        g_HangoutZonePull         = std::min<uint32_t>(U("PlayerbotsPlus.Hangout.ZonePull", 10), 100);
        g_HangoutZoneMoveSeconds  = std::clamp<uint32_t>(U("PlayerbotsPlus.Hangout.ZoneMoveSeconds", 15), 5, 600);
        g_HangoutZoneMovesAtOnce  = std::min<uint32_t>(U("PlayerbotsPlus.Hangout.ZoneMovesAtOnce", 3), 20);
        g_HangoutZones.clear();
        g_HangoutZonePlaces.clear();
        for (PbpHangoutSpot& spot : g_HangoutSpots)
            spot.zone = -1;
        auto spotAt = [](std::string const& key) -> PbpHangoutSpot*
        {
            for (PbpHangoutSpot& spot : g_HangoutSpots)
                if (spot.key == key)
                    return &spot;
            return nullptr;
        };

        if (B("PlayerbotsPlus.Hangout.Zones", true))
        {
            // The tiers: the code's, and any the conf adds or changes.
            std::map<std::string, PbpZones::Tier> tiers;   // by lower-case name
            std::string badTiers;
            auto addTier = [&tiers, &badTiers](std::string const& name, std::string const& value)
            {
                PbpZones::Tier tier;
                if (PbpZones::ParseTier(value, tier))
                    tiers[Lower(name)] = tier;
                else
                    badTiers += (badTiers.empty() ? "" : ", ") + name;
            };
            for (auto const& [name, value] : kDefaultZoneTiers)
                addTier(name, value);
            const std::string tierPrefix = "PlayerbotsPlus.Hangout.ZoneTier.";
            for (std::string const& key : sConfigMgr->GetKeysByString(tierPrefix))
                addTier(key.substr(tierPrefix.size()), sConfigMgr->GetOption<std::string>(key, ""));
            if (!badTiers.empty())
                LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.Hangout.ZoneTier: could not read {} (want "
                          "\"least start most\", in percent, each no more than the next).", badTiers);

            // The zones: the conf's; with none there and no spots of its own
            // either, the code's, for the code's spots. In order of their keys,
            // the same every start (the conf's come back in no order at all).
            std::vector<std::pair<std::string, std::string>> lines;
            const std::string prefix = "PlayerbotsPlus.Hangout.Zone.";
            for (std::string const& key : sConfigMgr->GetKeysByString(prefix))
                lines.emplace_back(key.substr(prefix.size()), sConfigMgr->GetOption<std::string>(key, ""));
            if (lines.empty() && sConfigMgr->GetKeysByString("PlayerbotsPlus.Hangout.Spot.").empty())
                for (auto const& [key, value] : kDefaultHangoutZones)
                    lines.emplace_back(key, value);
            std::sort(lines.begin(), lines.end());

            std::vector<std::string> problems;
            std::vector<PbpHangoutZone> found;
            std::unordered_map<std::string, std::string> taken;   // spot -> the zone it is in
            for (auto const& [key, value] : lines)
            {
                PbpZones::ZoneLine line;
                if (!PbpZones::ParseZoneLine(value, line))
                {
                    problems.push_back(key + " could not be read (want \"tier spot spot ...\")");
                    continue;
                }
                auto tier = tiers.find(line.tier);
                if (tier == tiers.end())
                {
                    problems.push_back(key + " has no tier called " + line.tier + " (Hangout.ZoneTier.*)");
                    continue;
                }
                PbpHangoutZone zone;
                zone.key   = key;
                zone.name  = key.substr(key.rfind('.') + 1);
                zone.tier  = line.tier;
                zone.min   = tier->second.min;
                zone.start = tier->second.start;
                zone.max   = tier->second.max;
                for (std::string const& spotKey : line.spots)
                {
                    PbpHangoutSpot const* spot = spotAt(spotKey);
                    const std::string city = spot ? PbpCityOf(spot->name) : std::string();
                    if (!spot)
                        problems.push_back(key + ": there is no hangout spot called " + spotKey);
                    else if (DuelSpot(spotKey))
                        problems.push_back(key + ": " + spotKey + " is where duels are fought (Duels.Spots) and keeps "
                                           "its own people");
                    else if (taken.contains(spotKey))
                        problems.push_back(key + ": " + spotKey + " is in " + taken[spotKey] + " already");
                    else if (!zone.city.empty() && city != zone.city)
                        problems.push_back(key + ": " + spotKey + " is in " + city + ", not " + zone.city);
                    else
                    {
                        zone.city = city;
                        zone.spots.push_back(spotKey);
                        taken[spotKey] = key;
                    }
                }
                if (zone.spots.empty())
                    problems.push_back(key + " has none of its spots, and is left out");
                else
                    found.push_back(std::move(zone));
            }

            // A city whose zones' shares could never add up to 100 has none.
            std::map<std::string, std::vector<PbpZones::Tier>> byCity;
            for (PbpHangoutZone const& zone : found)
                byCity[zone.city].push_back({ zone.min, zone.start, zone.max });
            for (auto const& [city, cityTiers] : byCity)
                if (!PbpZones::CanAddUp(cityTiers))
                {
                    uint32_t least = 0, most = 0;
                    for (PbpZones::Tier const& t : cityTiers)
                    {
                        least += t.min;
                        most += t.max;
                    }
                    problems.push_back(city + "'s zones are left out: their shares cannot add up to 100 (the least "
                                       "of them come to " + std::to_string(least) + ", the most to " +
                                       std::to_string(most) + ")");
                }
            for (PbpHangoutZone& zone : found)
            {
                if (!PbpZones::CanAddUp(byCity[zone.city]))
                    continue;
                for (std::string const& spotKey : zone.spots)
                    spotAt(spotKey)->zone = int(g_HangoutZones.size());
                g_HangoutZones.push_back(std::move(zone));
            }
            if (!problems.empty())
            {
                std::string all;
                for (std::string const& problem : problems)
                    all += (all.empty() ? "" : "; ") + problem;
                LOG_ERROR("server.loading", "[Playerbots Plus] PlayerbotsPlus.Hangout.Zone: {}.", all);
            }
        }

        // In a city with zones, nowhere is a hot spot: its zones say where most of its people are.
        std::unordered_set<std::string> zoned;
        for (PbpHangoutZone const& zone : g_HangoutZones)
            zoned.insert(zone.city);
        for (PbpHangoutSpot& spot : g_HangoutSpots)
            if (zoned.contains(PbpCityOf(spot.name)))
                spot.hot = false;

        if (g_HangoutCityWeights)
        {
            std::vector<PbpSpotShare> shares;
            for (PbpHangoutSpot const& spot : g_HangoutSpots)
                shares.push_back({ PbpCityOf(spot.name), spot.written, spot.hot });
            const std::vector<uint32_t> places =
                PbpShareHangoutPlaces(shares, PlayerbotsCityWeights(),
                                      std::min<uint32_t>(U("PlayerbotsPlus.Hangout.Places", 0), 5000),
                                      g_HangoutHotShare);
            for (size_t i = 0; i < g_HangoutSpots.size(); ++i)
                g_HangoutSpots[i].capacity = places[i];
        }
        else
            for (PbpHangoutSpot& spot : g_HangoutSpots)
                spot.capacity = spot.written;

        // A zoned city's places: its spots outside the zones as written (all
        // of them, if that is no more than it has), the rest its zones'; each
        // zoned spot's share at its zone's start, until the shares drift.
        for (std::string const& city : zoned)
        {
            uint64_t places = 0, outside = 0;
            std::vector<PbpHangoutSpot*> unzoned;
            for (PbpHangoutSpot& spot : g_HangoutSpots)
                if (PbpCityOf(spot.name) == city)
                {
                    places += spot.capacity;
                    if (spot.zone < 0)
                    {
                        outside += spot.written;
                        unzoned.push_back(&spot);
                    }
                }
            if (outside <= places)
            {
                for (PbpHangoutSpot* spot : unzoned)
                    spot->capacity = spot->written;
                g_HangoutZonePlaces[city] = uint32_t(places - outside);
            }
            else
            {
                std::vector<uint64_t> weights;
                for (PbpHangoutSpot* spot : unzoned)
                    weights.push_back(spot->written);
                const std::vector<uint32_t> parts = PbpApportion(uint32_t(places), weights);
                for (size_t i = 0; i < unzoned.size(); ++i)
                    unzoned[i]->capacity = parts[i];
                g_HangoutZonePlaces[city] = 0;
                LOG_WARN("server.loading", "[Playerbots Plus] {}'s spots outside its zones want room for {}, and it "
                         "has places for {}: none are left for its zones (PlayerbotsPlus.Hangout.Places, or the "
                         "capacities written for those spots).", city, outside, places);
            }
            std::vector<PbpZones::Tier> cityTiers;
            std::vector<std::vector<uint32_t>> written;
            std::vector<PbpHangoutZone const*> zones;
            for (PbpHangoutZone const& zone : g_HangoutZones)
                if (zone.city == city)
                {
                    zones.push_back(&zone);
                    cityTiers.push_back({ zone.min, zone.start, zone.max });
                    written.emplace_back();
                    for (std::string const& spotKey : zone.spots)
                        written.back().push_back(spotAt(spotKey)->written);
                }
            const std::vector<uint32_t> start = PbpZones::StartShares(cityTiers, [](uint32_t) { return 0u; });
            const std::vector<std::vector<uint32_t>> at =
                PbpZones::SpotPlaces(g_HangoutZonePlaces[city], start, written);
            for (size_t z = 0; z < zones.size(); ++z)
                for (size_t i = 0; i < zones[z]->spots.size(); ++i)
                    spotAt(zones[z]->spots[i])->capacity = at[z][i];
        }

        // A spot with room for more people than its radius holds grows to fit
        // them: RoomPerBot square yards each (crowded, the bots find nowhere
        // to stand). A zoned spot, the most its zone may hold. Up to the 60
        // yards a spot may have.
        std::unordered_map<std::string, uint32_t> most;
        for (PbpHangoutZone const& zone : g_HangoutZones)
        {
            std::vector<uint32_t> written;
            for (std::string const& spotKey : zone.spots)
                written.push_back(spotAt(spotKey)->written);
            const std::vector<uint32_t> at = PbpZones::SpotPlaces(
                PbpZones::AtMost(g_HangoutZonePlaces[zone.city], zone.max), { 100 }, { written })[0];
            for (size_t i = 0; i < zone.spots.size(); ++i)
                most[zone.spots[i]] = at[i];
        }
        const float room = std::clamp(F("PlayerbotsPlus.Hangout.RoomPerBot", 60.0f), 0.0f, 1000.0f);
        for (PbpHangoutSpot& spot : g_HangoutSpots)
        {
            const uint32_t people = std::max(spot.capacity, most.contains(spot.key) ? most[spot.key] : 0u);
            if (room > 0.0f)
                spot.radius = std::min(60.0f, std::max(spot.radius, std::sqrt(float(people) * room / 3.14159265f)));
        }
    }
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
    ListF("PlayerbotsPlus.Market.Price.RarityKneePct", p.rarityKneePct);
    p.rarityPower     = std::clamp(double(sConfigMgr->GetOption<float>("PlayerbotsPlus.Market.Price.RarityPower", 2.0f)),
                                   0.0, 4.0);
    p.rarityMaxFactor = std::clamp(
        double(sConfigMgr->GetOption<float>("PlayerbotsPlus.Market.Price.RarityMaxFactor", 100.0f)), 1.0, 10000.0);

    if (m.sellMinQuality > m.sellMaxQuality)
        std::swap(m.sellMinQuality, m.sellMaxQuality);

    PbpLedgerSettings& l = s.ledger;
    l.enable        = B("PlayerbotsPlus.Ledger.Enable", true);
    l.flushMinutes  = std::clamp<uint32_t>(U("PlayerbotsPlus.Ledger.FlushMinutes", 10), 1, 60);
    l.censusMinutes = std::clamp<uint32_t>(U("PlayerbotsPlus.Ledger.CensusMinutes", 30), 5, 1440);
    l.keepDays      = std::clamp<uint32_t>(U("PlayerbotsPlus.Ledger.KeepDays", 90), 7, 3650);
    // mod-ah-bot-plus's own setting: whose auctions are the AH bot's.
    for (uint64_t guid : PbpLedger::GuidList(sConfigMgr->GetOption<std::string>("AuctionHouseBot.GUIDs", "", false)))
        l.auctionBots.insert(uint32_t(guid));

    s.report.minutes = std::min<uint32_t>(U("PlayerbotsPlus.Report.Minutes", 10), 1440);
    s.report.file    = sConfigMgr->GetOption<std::string>("PlayerbotsPlus.Report.File", "PlayerbotsPlusReport.log");
    if (s.report.file.empty())
        s.report.file = "PlayerbotsPlusReport.log";

    PbpBotDungeonSettings& d = s.dungeons;
    d.enable              = B("PlayerbotsPlus.BotDungeons.Enable", false);
    d.checkSeconds        = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.CheckSeconds", 30), 5, 3600);
    d.maxRuns             = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.MaxRuns", 5), 100);
    d.maxTickMs           = U("PlayerbotsPlus.BotDungeons.MaxWorldTickMs", 200);
    d.minFreeMemoryMb     = U("PlayerbotsPlus.BotDungeons.MinFreeMemoryMB", 1024);
    d.needPlayerOnline    = B("PlayerbotsPlus.BotDungeons.OnlyWithPlayersOnline", true);
    d.questRequired       = B("PlayerbotsPlus.BotDungeons.QuestRequired", true);
    d.queueMinutes        = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.QueueMinutes", 15), 1, 240);
    // Alone for as long as the whole wait, a party never goes as one.
    d.aloneMinutes        = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.AloneMinutes", 2), d.queueMinutes - 1);
    d.cooldownMinutes     = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.CooldownMinutes", 15), 1440);
    d.failCooldownMinutes = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.FailCooldownMinutes", 30), 1440);
    d.noProgressMinutes   = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.NoProgressMinutes", 10), 240);
    d.corpseRuns          = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.CorpseRuns", 5), 50);
    d.wipeRegroups        = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.WipeRegroups", 2), 10);
    d.healerStepIn        = B("PlayerbotsPlus.BotDungeons.HealerStepIn", true);
    d.healerDualSpec      = B("PlayerbotsPlus.BotDungeons.HealerDualSpec", true);
    d.maxRunMinutes       = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.MaxRunMinutes", 120), 10, 1440);
    d.lootSeconds         = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.LootSeconds", 30), 600);
    d.avoidAfterFailures  = U("PlayerbotsPlus.BotDungeons.AvoidAfterFailures", 3);
    d.avoidMinutes        = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.AvoidMinutes", 120), 10080);
    {
        std::string bad;
        d.bands = PbpDungeons::ParseBands(
            sConfigMgr->GetOption<std::string>("PlayerbotsPlus.BotDungeons.LevelBands", kDefaultLevelBands), bad);
        if (!bad.empty())
            LOG_ERROR("module.playerbotsplus",
                      "[Playerbots Plus] PlayerbotsPlus.BotDungeons.LevelBands: '{}' left out (a band is low-high)", bad);
        d.bandShares = PbpDungeons::ParseShares(
            sConfigMgr->GetOption<std::string>("PlayerbotsPlus.BotDungeons.LevelBandRuns", kDefaultLevelBandRuns),
            d.bands.size(), bad);
        if (!bad.empty())
            LOG_ERROR("module.playerbotsplus",
                      "[Playerbots Plus] PlayerbotsPlus.BotDungeons.LevelBandRuns: '{}' is not a number; 1 instead",
                      bad);
    }
    d.quests              = B("PlayerbotsPlus.BotDungeons.Quests", true);
    d.questRange          = std::clamp(F("PlayerbotsPlus.BotDungeons.QuestRange", 1500.0f), 50.0f, 20000.0f);
    d.questWalksPerMinute = std::min<uint32_t>(U("PlayerbotsPlus.BotDungeons.QuestWalksPerMinute", 10), 600);
    d.questWalkMinutes    = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.QuestWalkMinutes", 15), 1, 240);
    d.questLookMinutes    = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.QuestLookMinutes", 30), 1, 1440);
    d.questRuns           = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.QuestRuns", 4), 1, 100);
    d.keepLogFree         = std::clamp<uint32_t>(U("PlayerbotsPlus.BotDungeons.KeepLogFree", 3), 2, 10);

    PbpGroupQuestSettings& g = s.groupQuests;
    g.enable            = B("PlayerbotsPlus.GroupQuests.Enable", false);
    g.range             = std::clamp(F("PlayerbotsPlus.GroupQuests.QuestRange", 1500.0f), 50.0f, 20000.0f);
    g.walksPerMinute    = std::min<uint32_t>(U("PlayerbotsPlus.GroupQuests.WalksPerMinute", 10), 600);
    g.walkMinutes       = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.WalkMinutes", 15), 1, 240);
    g.lookMinutes       = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.LookMinutes", 20), 1, 1440);
    g.keepLogFree       = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.KeepLogFree", 3), 2, 10);
    g.partySize         = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.PartySize", 3), 2, 5);
    g.askMinutes        = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.AskMinutes", 15), 1, 240);
    g.askForMinutes     = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.AskForMinutes", 60), 0, 1440);
    g.asksPerMinute     = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.AsksPerMinute", 4), 1, 60);
    g.meetMinutes       = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.MeetMinutes", 15), 1, 60);
    g.attempts          = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.Attempts", 3), 1, 20);
    g.noProgressMinutes = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.NoProgressMinutes", 10), 2, 120);
    g.deaths            = std::clamp<uint32_t>(U("PlayerbotsPlus.GroupQuests.Deaths", 3), 1, 20);
    g.cooldownMinutes   = std::min<uint32_t>(U("PlayerbotsPlus.GroupQuests.CooldownMinutes", 30), 1440);

    g_Pbp = std::move(s);

    pbp::g_PbpDebug = B("PlayerbotsPlus.Debug", false);
    LoadHangout();
    LoadCityLife();
    LoadStuckReviveMarks();
    LoadPetsDuels();
    LoadHangoutZones();
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

    uint32_t places = 0, hotPlaces = 0;
    for (pbp::PbpHangoutSpot const& spot : pbp::g_HangoutSpots)
    {
        places += spot.capacity;
        hotPlaces += spot.hot ? spot.capacity : 0;
    }
    std::string const shared = pbp::g_HangoutCityWeights
        ? " by playerbots' city weights, " + std::to_string(hotPlaces) + " of them at the hot spots"
        : std::string();
    LOG_INFO(where,
             "[Playerbots Plus] City hangouts {} ({} spots, room for {}{}; fetched from elsewhere {}), stuck mounts "
             "{}, on foot indoors {}, out of fires {}, room to stand {}, walks of bots playerbots is not running made "
             "at once {} (nobody within {:.0f} yd), getting back up {}, raid marks {} (sapping the star {}).",
             pbp::g_HangoutEnable ? "on" : "off", pbp::g_HangoutSpots.size(), places, shared,
             pbp::g_HangoutBringIn ? "on" : "off", pbp::g_StuckEnable ? "on" : "off",
             pbp::g_IndoorsEnable ? "on" : "off", pbp::g_FireEnable ? "on" : "off",
             pbp::g_SpaceEnable ? "on" : "off", pbp::g_InactiveEnable ? "on" : "off", pbp::g_InactiveUnseenYards,
             pbp::g_ReviveEnable ? "on" : "off", pbp::g_MarksEnable ? "on" : "off",
             pbp::g_MarksSapStar ? "on" : "off");

    if (!pbp::g_HangoutZones.empty())
    {
        std::map<std::string, std::string> byCity;   // city -> its zones, where they start
        for (pbp::PbpHangoutZone const& zone : pbp::g_HangoutZones)
            byCity[zone.city] += (byCity[zone.city].empty() ? "" : ", ") + zone.name + " " +
                                 std::to_string(zone.start) + "%";
        std::string cities;
        for (auto const& [city, zones] : byCity)
            cities += (cities.empty() ? "" : "; ") + city + " (" +
                      std::to_string(pbp::g_HangoutZonePlaces[city]) + " places: " + zones + ")";
        LOG_INFO(where,
                 "[Playerbots Plus] City hangout zones: {}. Their shares drift every {}s, by up to {} points; up to "
                 "{} bots a city move between them every {}s.",
                 cities, pbp::g_HangoutZoneShiftSeconds, pbp::g_HangoutZoneShiftStep, pbp::g_HangoutZoneMovesAtOnce,
                 pbp::g_HangoutZoneMoveSeconds);
    }

    if (pbp::g_LifeEnable)
    {
        std::string kinds;
        for (auto const& [name, t] : pbp::g_LifeTemperaments)
            kinds += (kinds.empty() ? "" : ", ") + name;
        std::string shares;
        for (auto const& [name, share] : pbp::g_LifeTemperamentShares)
            shares += (shares.empty() ? "" : ", ") + name + " " + std::to_string(share) + "%";
        LOG_INFO(where,
                 "[Playerbots Plus] City life on, within {:.0f} yd of a real player: {}% parked, {}% restless, the "
                 "rest fidget every {}-{} s; hops {}%, mounts rearing {}% (done back {}%), runs about {}%, errands "
                 "{}%, fishing {}%; rare mounts looked at {}, dance parties {}; flights in {}%, out {}%. "
                 "Temperaments: {} (without a chat module: {}); showing off about every {} min.",
                 pbp::g_LifeRange, pbp::g_LifeParkedShare, pbp::g_LifeRestlessShare, pbp::g_LifeShuffleMinSeconds,
                 pbp::g_LifeShuffleMaxSeconds, pbp::g_LifeJumpChance, pbp::g_LifeRearChance,
                 pbp::g_LifeRearBackChance, pbp::g_LifeWanderChance, pbp::g_LifeErrandChance,
                 pbp::g_LifeFish ? pbp::g_LifeFishChance : 0u, pbp::g_LifeGawk ? "on" : "off",
                 pbp::g_LifeDance ? "on" : "off", pbp::g_LifeFlyInChance, pbp::g_LifeFlyOutChance,
                 kinds.empty() ? "none" : kinds, shares.empty() ? "everyone the default" : shares + ", the rest the default",
                 pbp::g_LifeShowOffMinutes);
    }
    else
        LOG_INFO(where, "[Playerbots Plus] City life off.");

    LOG_INFO(where,
             "[Playerbots Plus] Economy ledger {} (written every {} min, a look at the bots every {} min, kept {} "
             "days; the AH bot's characters: {}).",
             g_Pbp.ledger.enable ? "on" : "off", g_Pbp.ledger.flushMinutes, g_Pbp.ledger.censusMinutes,
             g_Pbp.ledger.keepDays, g_Pbp.ledger.auctionBots.size());
    if (g_Pbp.report.minutes)
        LOG_INFO(where, "[Playerbots Plus] The full report goes to {} every {} min, and whenever .pbp report asks.",
                 g_Pbp.report.file, g_Pbp.report.minutes);
    else
        LOG_INFO(where, "[Playerbots Plus] The full report goes to {} whenever .pbp report asks.", g_Pbp.report.file);

    PbpBotDungeonSettings const& bd = g_Pbp.dungeons;
#if PBP_BOT_DUNGEONS
    if (bd.enable)
    {
        std::string bands;
        for (size_t i = 0; i < bd.bands.size(); ++i)
            bands += (bands.empty() ? "; by level " : ", ") + PbpDungeons::BandName(bd.bands[i]) + " x" +
                     std::to_string(i < bd.bandShares.size() ? bd.bandShares[i] : 1);
        LOG_INFO(where,
                 "[Playerbots Plus] Bot dungeons on (up to {} at once{}{}{}{}; {}; with nobody free to heal, {}; "
                 "dungeon quests {}).",
                 bd.maxRuns,
                 bd.maxTickMs ? ", while the world tick averages " + std::to_string(bd.maxTickMs) + " ms or less"
                              : std::string(),
                 bd.minFreeMemoryMb ? ", while " + std::to_string(bd.minFreeMemoryMb) + " MB of memory or more is free"
                                    : std::string(),
                 bd.needPlayerOnline ? ", only with a player online" : "", bands,
                 bd.questRequired ? "a party only goes where one of it holds a quest" : "quests or not",
                 !bd.healerStepIn    ? "no party"
                 : bd.healerDualSpec ? "a damage dealer heals instead, on a second spec once it can have one"
                                     : "a damage dealer heals instead, in its own talents",
                 bd.quests ? "fetched, kept, handed in" : "left to playerbots");
        if (bd.minFreeMemoryMb && PbpDungeons_FreeMemoryMb() < 0)
            LOG_WARN(where, "[Playerbots Plus] PlayerbotsPlus.BotDungeons.MinFreeMemoryMB is set, but there is no "
                            "/proc/meminfo to read here: new parties do not wait for memory.");
    }
    else
        LOG_INFO(where, "[Playerbots Plus] Bot dungeons off.");
#else
    if (bd.enable)
        LOG_WARN(where, "[Playerbots Plus] PlayerbotsPlus.BotDungeons.Enable is on, but this build has no "
                        "mod-dungeon-clear (DcBotRun.h): bot dungeons stay off.");
#endif

    PbpGroupQuestSettings const& gq = g_Pbp.groupQuests;
    if (gq.enable)
        LOG_INFO(where,
                 "[Playerbots Plus] Group quests on (taken within {} yd; help asked for every {} min for {} min, "
                 "parties of {} where a quest suggests none; {} tries, {} min with nothing moving forward is one).",
                 uint32_t(gq.range), gq.askMinutes, gq.askForMinutes, gq.partySize, gq.attempts,
                 gq.noProgressMinutes);
    else
        LOG_INFO(where, "[Playerbots Plus] Group quests off.");

    size_t names = 0;
    for (auto const& [family, list] : pbp::g_PetNames)
        names += list.size();
    std::string duelSpots;
    for (std::string const& key : pbp::g_DuelsSpots)
        duelSpots += (duelSpots.empty() ? "" : ", ") + key;
    LOG_INFO(where,
             "[Playerbots Plus] Hunters naming their pets {} ({} names), companions {} ({} in the list), duels {} "
             "(at {}).",
             pbp::g_PetNamesEnable ? "on" : "off", names, pbp::g_CompanionsEnable ? "on" : "off",
             pbp::g_Companions.size(), pbp::g_DuelsEnable ? "on" : "off",
             duelSpots.empty() ? std::string("nowhere") : duelSpots);
}
