#ifndef PLAYERBOTS_PLUS_CONFIG_H
#define PLAYERBOTS_PLUS_CONFIG_H

#include "PlayerbotsPlusMarketPrice.h"

#include <array>
#include <cstdint>
#include <unordered_set>

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

// Reads the conf into g_Pbp. `reload`: .reload config or .pbp reload, not startup.
void PlayerbotsPlus_LoadConfig(bool reload);

#endif // PLAYERBOTS_PLUS_CONFIG_H
