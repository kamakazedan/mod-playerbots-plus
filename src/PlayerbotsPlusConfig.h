#ifndef PLAYERBOTS_PLUS_CONFIG_H
#define PLAYERBOTS_PLUS_CONFIG_H

#include "PlayerbotsPlusMarketPrice.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// --------------------------------------------------------------------------
// Settings, from mod_playerbots_plus.conf. Read on the world thread (startup,
// .reload config, .pbp reload) and only read there: nothing in this module
// runs anywhere else.
// --------------------------------------------------------------------------

struct PbpMarketSettings
{
    bool     enable                 = true;
    uint32_t intervalMs             = 10000;   // how often a bot or two goes to the auction house
    uint32_t visitsPerInterval      = 1;
    uint32_t cooldownMinMinutes     = 40;      // a bot goes again after this...
    uint32_t cooldownMaxMinutes     = 120;     // ...to this
    uint32_t minLevel               = 10;
    std::unordered_set<uint32_t> extraZones;   // besides the capitals: Shattrath, Dalaran

    bool     sell                   = true;
    uint32_t sellMinQuality         = 1;       // common (white)
    uint32_t sellMaxQuality         = 4;       // epic
    uint32_t maxListingsPerVisit    = 3;
    uint32_t maxAuctionsPerBot      = 8;
    uint32_t maxBotAuctionsPerHouse = 2000;
    uint32_t maxRelists             = 2;       // listed again at most this often after coming back unsold
    std::array<uint32_t, 3> durationWeights { 20, 40, 40 };   // 12, 24 and 48 hours
    uint32_t maxBuyoutGold          = 50000;

    bool     buy                    = true;
    bool     buyUpgrades            = true;
    bool     buyMaterials           = true;
    uint32_t maxBuysPerVisit        = 2;
    uint32_t buySharePct            = 25;      // of its gold, for any one thing
    uint32_t reservePerLevel        = 2000;    // copper per level never spent here: repairs, training
    uint32_t upgradeMinQuality      = 2;       // uncommon (green)
    uint32_t upgradeLevelWindow     = 6;       // gear made for its level, or up to this many below
    uint32_t materialsOverValuePct  = 200;
    uint32_t maxUsageChecks         = 30;      // playerbots' item judgement asked at most this often a visit

    bool     collectMail            = true;
    bool     equipBought            = true;
    bool     logTrades              = false;

    PbpPrice::Settings price;
};

struct PbpSettings
{
    bool              enable = true;
    PbpMarketSettings market;
};

extern PbpSettings g_Pbp;

// The features that came from mod-ollama-chat keep the names their settings
// had there (g_HangoutEnable...), in a namespace of their own (pbp): every
// module is linked into one worldserver, and a global of the same name in
// another module would clash. Their files say `using namespace pbp;`.
namespace pbp
{
    extern bool g_PbpDebug;   // PlayerbotsPlus.Debug: more of what they do, at INFO

    // City hangouts (PlayerbotsPlusHangout)
    struct PbpHangoutSpot
    {
        std::string key;        // PlayerbotsPlus.Hangout.Spot.<key>
        std::string name;       // "the Trade District in Stormwind"
        uint32_t    map      = 0;
        float       x        = 0.0f;
        float       y        = 0.0f;
        float       z        = 0.0f;
        float       radius   = 15.0f;
        uint32_t    capacity = 0;
    };
    extern bool        g_HangoutEnable;
    extern std::vector<PbpHangoutSpot> g_HangoutSpots;
    extern uint32_t    g_HangoutCheckSeconds;
    extern float       g_HangoutRecruitRange;
    extern uint32_t    g_HangoutRecruitChance;
    extern uint32_t    g_HangoutStayMinutesMin;
    extern uint32_t    g_HangoutStayMinutesMax;
    extern uint32_t    g_HangoutCooldownMinutes;
    extern uint32_t    g_HangoutTravelSeconds;
    extern uint32_t    g_HangoutMountedChance;
    extern uint32_t    g_HangoutBenchChance;
    extern uint32_t    g_HangoutSitChance;
    extern uint32_t    g_HangoutAfkChance;
    extern float       g_HangoutBenchRange;
    extern float       g_HangoutHeightTolerance;
    extern std::vector<std::string> g_HangoutBenchWords;   // lowercase
    extern bool        g_HangoutBringIn;
    extern uint32_t    g_HangoutBringInFill;
    extern uint32_t    g_HangoutBringInPerMinute;
    extern uint32_t    g_HangoutBringInMinLevel;
    extern float       g_HangoutArrivalRange;

    // Stuck on a mount (PlayerbotsPlusStuck)
    extern bool        g_StuckEnable;
    extern uint32_t    g_StuckMountCount;
    extern uint32_t    g_StuckWindowSeconds;
    extern float       g_StuckRadius;
    extern uint32_t    g_StuckIgnoreMinutes;
    extern uint32_t    g_StuckWalkMinutes;

    // Getting back up (PlayerbotsPlusRevive)
    extern bool        g_ReviveEnable;
    extern uint32_t    g_ReviveReleaseDelaySeconds;
    extern uint32_t    g_ReviveWaitForRezSeconds;
    extern uint32_t    g_ReviveRunBackSeconds;

    // Raid marks (PlayerbotsPlusMarks)
    extern bool        g_MarksEnable;
    extern std::string g_MarksClassIcon[16];   // by class id: the mark it controls
    extern bool        g_MarksAddCcStrategy;
    extern bool        g_MarksSapStar;
    extern bool        g_MarksCrossToSkull;   // the skull moves onto the cross when its mob dies
    extern bool        g_MarksAutoSkull;      // ...or onto the next loose mob
    extern uint32_t    g_MarksInUseSeconds;
    extern uint32_t    g_MarksSapStealthWaitSeconds;
    extern uint32_t    g_MarksSapApproachSeconds;
    extern uint32_t    g_MarksSapWaitSeconds;
}

// Reads the conf into g_Pbp and the pbp:: settings. `reload`: .reload config
// or .pbp reload, not startup.
void PlayerbotsPlus_LoadConfig(bool reload);

#endif // PLAYERBOTS_PLUS_CONFIG_H
