#ifndef PLAYERBOTS_PLUS_CONFIG_H
#define PLAYERBOTS_PLUS_CONFIG_H

#include "PlayerbotsPlusDungeonsText.h"
#include "PlayerbotsPlusMarketPrice.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
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

// The economy ledger (PlayerbotsPlusLedger).
struct PbpLedgerSettings
{
    bool     enable        = true;
    uint32_t flushMinutes  = 10;   // figures written this often
    uint32_t censusMinutes = 30;   // a look at all the bots this often
    uint32_t keepDays      = 90;
    std::unordered_set<uint32_t> auctionBots;   // AuctionHouseBot.GUIDs: the AH bot's characters
};

// Bot dungeons (PlayerbotsPlusDungeons, PlayerbotsPlusDungeonQuests).
struct PbpBotDungeonSettings
{
    bool     enable               = false;
    uint32_t checkSeconds         = 30;      // the planner looks for a party this often
    uint32_t maxRuns              = 5;       // parties in the queue and runs going on, together
    uint32_t maxTickMs            = 200;     // no new party while the world tick averages more; 0: no check
    uint32_t minFreeMemoryMb      = 1024;    // ...nor while the machine has less memory available; 0: no check
    bool     needPlayerOnline     = true;    // ...nor while no player is online
    bool     questRequired        = true;    // a party only goes where one of it holds a quest
    uint32_t aloneMinutes         = 2;       // queued each alone this long (a player may be matched in), then as a party
    uint32_t queueMinutes         = 15;      // the longest a party waits in the queue
    uint32_t cooldownMinutes      = 15;      // after a run, before a bot queues again
    uint32_t failCooldownMinutes  = 30;      // ...after one that failed
    uint32_t noProgressMinutes    = 10;      // a run with nothing moving forward this long fails
    uint32_t maxRunMinutes        = 120;     // and any run this long
    uint32_t lootSeconds          = 30;      // after the last boss, before they leave
    uint32_t avoidAfterFailures   = 3;       // a dungeon whose runs failed this many times in a row...
    uint32_t avoidMinutes         = 120;     // ...is not gone to for this long
    std::vector<PbpDungeons::Band> bands;    // the runs shared out by the dungeon's level; none: any dungeon
    std::vector<uint32_t> bandShares;        // how many runs each band is meant to hold, one per band

    bool     quests               = true;    // take, keep, hand in and let go of dungeon quests
    float    questRange           = 1500.0f; // yards: the farthest a bot walks to a giver
    uint32_t questWalksPerMinute  = 10;      // bots sent to a giver or to hand in, all told
    uint32_t questWalkMinutes     = 15;      // a walk not there by then is given up
    uint32_t questLookMinutes     = 30;      // a bot is looked at for quests to take this often
    uint32_t questRuns            = 4;       // runs of its dungeon that do not finish a quest before it goes
    uint32_t keepLogFree          = 3;       // quest log slots kept free while it holds a dungeon quest
};

struct PbpSettings
{
    bool                  enable = true;
    PbpMarketSettings     market;
    PbpLedgerSettings     ledger;
    PbpBotDungeonSettings dungeons;
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
        bool        hot      = false;   // one of its city's busiest places (Hangout.HotSpots)
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
    extern uint32_t    g_HangoutFullMinutes;
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
    extern uint32_t    g_HangoutInnArrivalSeconds;   // one arrival at a city's inns per this, while someone is there
    extern bool        g_HangoutCityWeights;   // places shared out by playerbots' city weights
    extern uint32_t    g_HangoutHotShare;      // ...that share of a city's at its hot spots (percent)

    // Stuck on a mount (PlayerbotsPlusStuck)
    extern bool        g_StuckEnable;
    extern uint32_t    g_StuckMountCount;
    extern uint32_t    g_StuckWindowSeconds;
    extern float       g_StuckRadius;
    extern uint32_t    g_StuckIgnoreMinutes;
    extern uint32_t    g_StuckWalkMinutes;

    // Out of the fire (PlayerbotsPlusFire)
    extern bool        g_FireEnable;
    extern float       g_FireMargin;

    // On foot indoors (PlayerbotsPlusIndoors)
    extern bool        g_IndoorsEnable;
    extern uint32_t    g_IndoorsSeconds;

    // Room to stand (PlayerbotsPlusSpace)
    extern bool        g_SpaceEnable;
    extern uint32_t    g_SpaceCheckSeconds;
    extern uint32_t    g_SpaceStillSeconds;
    extern float       g_SpaceDistance;
    extern float       g_SpaceStepMin;
    extern float       g_SpaceStepMax;
    extern uint32_t    g_SpaceCooldownSeconds;
    extern float       g_SpaceRange;

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

    // Hunter pets' names (PlayerbotsPlusPets)
    extern bool        g_PetNamesEnable;
    extern uint32_t    g_PetNameChance;       // of the random bots' hunters: the rest keep the name it was tamed with
    extern uint32_t    g_PetNamesPerMinute;
    // "cat" -> the names a cat may get; "any" -> the ones any pet may.
    extern std::unordered_map<std::string, std::vector<std::string>> g_PetNames;

    // Companions (PlayerbotsPlusPets)
    struct PbpCompanionEntry
    {
        uint32_t item     = 0;    // the item that teaches it
        uint32_t minLevel = 1;    // where it comes from is not somewhere a lower level goes
        uint32_t weight   = 10;   // how common it is
        uint8_t  side     = 0;    // 0 both sides, 1 the Alliance, 2 the Horde
        uint8_t  holiday  = 0;    // PbpHolidayKey of the holiday it comes from, 0 none
    };
    extern bool        g_CompanionsEnable;
    extern std::vector<PbpCompanionEntry> g_Companions;
    extern uint32_t    g_CompanionsOwnChance;         // of the random bots: own any at all
    extern uint32_t    g_CompanionsCollectorChance;   // of those: collect them, the rare ones too
    extern uint32_t    g_CompanionsCommonWeight;      // this weight and up is one anybody might have
    extern uint32_t    g_CompanionsOutChance;         // of all the bots near someone: have one out
    extern uint32_t    g_CompanionsSwitchChance;      // of those: swap it for another once in a stretch idle
    extern uint32_t    g_CompanionsHolidayBoost;      // a holiday's own, in season: this many times as likely
    extern uint32_t    g_CompanionsCheckSeconds;
    extern float       g_CompanionsRange;             // from a real player

    // Duels at the city gates (PlayerbotsPlusDuels)
    extern bool        g_DuelsEnable;
    extern std::vector<std::string> g_DuelsSpots;  // hangout spots where people duel: "OrgrimmarGates"
    extern uint32_t    g_DuelsCheckSeconds;
    extern uint32_t    g_DuelsChance;             // a check at a spot starts one
    extern float       g_DuelsWatchRange;         // ...while a real player is this close to its middle
    extern uint32_t    g_DuelsLevelGap;
    extern uint32_t    g_DuelsMinLevel;
    extern uint32_t    g_DuelsBotCooldownMinutes;
    extern uint32_t    g_DuelsMaxAtOnce;          // at one spot
    extern uint32_t    g_DuelsChallengePlayerChance;   // a check challenges a real player standing there
    extern uint32_t    g_DuelsChallengePlayerMinutes;  // ...and not them again for this long

    // A hangout spot people duel at (Duels.Spots), with duels on.
    bool DuelSpot(std::string const& key);
}

// Reads the conf into g_Pbp and the pbp:: settings. `reload`: .reload config
// or .pbp reload, not startup.
void PlayerbotsPlus_LoadConfig(bool reload);

#endif // PLAYERBOTS_PLUS_CONFIG_H
