#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlus.h"
#include "PlayerbotsPlusCityLifeText.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusFire.h"
#include "PlayerbotsPlusGroupQuests.h"
#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusText.h"
#include "PlayerbotsPlusUtil.h"

#include "Bag.h"
#include "CellImpl.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "Item.h"
#include "ItemPackets.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "MapMgr.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "PathGenerator.h"
#include "Player.h"
#include "SpellAuraDefines.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "Event.h"
#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "TravelMgr.h"
#include "TravelNode.h"

#include <fmt/core.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <list>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr float    kTwoPi      = 6.2831853f;
    constexpr uint32_t kPointId    = 0x48474F;   // MovePoint id for the last few steps
    // The bot's own AI waits this long; renewed every tick, so a slow tick is no gap.
    constexpr uint32_t kHoldMs     = 5000;
    constexpr float    kMaxClimb   = 30.0f;      // a bot this far above or below a spot is on another level
    constexpr auto     kMountGrace = std::chrono::seconds(10);

    // What "in sight of a spot's middle" looks through: walls and buildings,
    // a door, but not a gameobject's small model (a mailbox, a crate, a
    // holiday decoration) -- the core leaves those out for a gameobject's
    // own casts the same way. Most spots' middles are a mailbox, and the look
    // starts two yards up inside it, which left eight of them where nobody
    // ever stood: every place out of sight.
    const LineOfSightChecks kSightChecks = LineOfSightChecks(LINEOFSIGHT_ALL_CHECKS & ~LINEOFSIGHT_CHECK_GOBJECT_M2);
    // A spot that has looked at this many places and found none says so in
    // the log, once.
    constexpr uint64_t kNobodyAfter = 960;

    // Set while a bench is used here, so a chat module's "used an object"
    // chatter leaves it alone (PlayerbotsPlus::UsingBench).
    thread_local bool t_usingBench = false;

    enum class Posture : uint8_t
    {
        Mounted,
        Bench,
        Sit,
        Stand
    };

    enum class Phase : uint8_t
    {
        Arriving,   // fetched from elsewhere: teleported in, not landed yet
        Travel,     // playerbots' "go camp" is taking it there
        Approach,   // the last few steps, to its own place in the crowd
        Settled,
        Duelling,   // at the gates: in a duel, or getting its breath back after one, its own AI running
        Flying,     // fetched: flying in to the city by its flight path (CityLife.FlyInChance), its own AI flying it
        Errand      // off to a banker, an auctioneer or a mailbox in the city, and then on (CityLife.Errand*)
    };

    // What city life (CityLife.*) has it doing besides standing about.
    enum class Act : uint8_t
    {
        None,
        Gawking,    // over looking at someone's rare mount
        Dancing,    // dancing, in a dance party or with someone dancing (or about to join one)
        Chatting,   // over by who it is talking to in /say (a chat module's)
        Fishing     // at the water's edge with a line out
    };

    struct Visit
    {
        std::string       spotKey;
        std::string       spotName;
        uint32_t          map    = 0;
        float             x      = 0.0f;   // its place in the spot
        float             y      = 0.0f;
        float             z      = 0.0f;
        float             cx     = 0.0f;   // the spot's middle
        float             cy     = 0.0f;
        float             cz     = 0.0f;
        float             radius = 0.0f;
        Phase             phase    = Phase::Travel;
        Posture           posture  = Posture::Stand;
        bool              wantsAfk = false;
        bool              afkSet   = false;   // the AFK flag is this module's
        bool              holding  = false;   // its playerbots AI is held: nothing undoes the sitting or the AFK
        ObjectGuid        bench;              // a bench it is going to, or sat on
        float             bx = 0.0f, by = 0.0f, bz = 0.0f;
        uint8_t           seat = 0;           // the stand state sitting left it in (a chair's is its own)
        Clock::time_point started{};
        Clock::time_point approachStart{};
        Clock::time_point settledAt{};
        Clock::time_point until{};
        Clock::time_point nextLook{};
        Clock::time_point cameBack{};         // was AFK, then talked to
        Clock::time_point landedAt{};         // fetched: when it was first seen in the city
        Clock::time_point nextTry{};          // ...and when it next looks for room
        Clock::time_point duelOver{};         // its duel ended (Duelling)
        bool              forDuel   = false;  // sent out to the gates to fight someone (PbpHangout_SendToDuel)
        bool              broughtIn = false;  // fetched from elsewhere (Hangout.BringIn)...
        bool              viaInn    = false;  // ...in at an inn, to walk over from
        uint32_t          fromMap   = 0;      // ...from here
        float             fromX = 0.0f, fromY = 0.0f, fromZ = 0.0f, fromO = 0.0f;

        // City life (CityLife.*)
        PbpCityLife::Temper temper = PbpCityLife::Temper::Shuffler;
        bool              lifeSet     = false;   // its temper rolled (at its first settle here)
        Clock::time_point nextFidget{};
        Clock::time_point nextTurn{};
        uint32_t          hops        = 0;       // hops left in this little bout
        Clock::time_point nextHop{};
        Clock::time_point busyUntil{};           // a chat module has it talking or emoting
        ObjectGuid        turnTo;                // someone spoke near it: it looks round at them at turnAt
        Clock::time_point turnAt{};
        Act               act         = Act::None;
        ObjectGuid        actWith;               // the rider, the one dancing, the one it talks to
        Clock::time_point actUntil{};
        Clock::time_point danceJoinAt{};         // a dance it is to join: when it goes over
        bool              danceJoined = false;
        bool              danceSaid   = false;   // ...and it was told it joined (CityDance)
        bool              away        = false;   // off its own place for the act: back to hx, hy, hz after
        float             hx = 0.0f, hy = 0.0f, hz = 0.0f;
        uint8_t           errandStep  = 0;       // Errand: 0 on its way, 1 at it
        float             ex = 0.0f, ey = 0.0f, ez = 0.0f;   // ...where it stands
        float             efx = 0.0f, efy = 0.0f;             // ...what it faces
        Clock::time_point errandSince{};
        Clock::time_point errandUntil{};
        bool              errandFaced = false;
        float             fishO       = 0.0f;    // Fishing: facing the water
        ObjectGuid        oldMain, oldOff;       // ...its weapons, back in its hands after
        bool              poleOn      = false;
        Clock::time_point nextCast{};
        uint32_t          castFails   = 0;
        Clock::time_point flewAt{};              // Flying: last seen in the air
        Clock::time_point rearAt{};              // someone near reared their mount: it does it back then
        ObjectGuid        rearTo;                // ...at them
        Clock::time_point rearedAt{};            // its own mount last reared
    };

    std::unordered_map<uint64_t, Visit>             g_visits;     // by bot guid
    std::unordered_map<uint64_t, Clock::time_point> g_cooldown;   // bot -> may hang out again from
    PbpHangoutStats                                    g_stats;

    // Which zone each spot is in, worked out once from the map.
    struct SpotZone
    {
        uint32_t map  = 0;
        float    x    = 0.0f;
        float    y    = 0.0f;
        uint32_t zone = 0;
    };
    std::unordered_map<std::string, SpotZone> g_spotZones;

    // Where the bots fetched for a spot arrive while someone is in the city:
    // an inn near it, the way people come in by hearthstone. Worked out once
    // per spot from the creature spawns.
    struct Inn
    {
        float    x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
        float    dist    = 0.0f;
        uint32_t faction = 0;   // the innkeeper's faction template
    };
    struct SpotInns
    {
        uint32_t         map = 0;
        float            x = 0.0f, y = 0.0f;   // the spot they were found for
        bool             anyZone = false;      // ...a duel spot, which takes the city's inn through the gate
        bool             known = false;
        std::vector<Inn> inns;                 // nearest first
    };
    std::unordered_map<std::string, SpotInns> g_spotInns;

    std::deque<Clock::time_point>  g_fetchTimes;   // bots fetched in the last minute
    std::unordered_map<std::string, Clock::time_point> g_spotNextFetch;   // a spot with no room fetches again from then

    // A spot found full: nowhere left there that checks out, with `held`
    // bots there or on their way. It takes nobody more until fewer are there
    // or Hangout.FullMinutes have gone by, and the rest of its room goes to
    // spots that can take people, its own city's first (PbpShareFullRoom).
    struct Full
    {
        uint32_t          held = 0;
        Clock::time_point since{};
    };
    std::unordered_map<std::string, Full>     g_full;   // by spot key
    std::unordered_map<std::string, uint32_t> g_room;   // each spot's room, as last given out (ShareRoom)

    // How the places looked at for each spot went, since startup: what
    // .pbp hangouts shows, to tell a spot that is full from one nobody can
    // stand at.
    struct Looks
    {
        uint64_t found = 0;    // a place taken up
        uint64_t ground = 0;   // no ground there, or not at the middle's height
        uint64_t water = 0, sight = 0, taken = 0, roof = 0, fire = 0, crowded = 0, path = 0;
        uint64_t noRoom = 0;   // a bot fetched there found nowhere at all
        uint64_t full = 0;     // times found full
        bool     said = false; // "nobody can stand here" logged

        uint64_t Looked() const { return found + ground + water + sight + taken + roof + fire + crowded + path; }
    };
    std::unordered_map<std::string, Looks> g_looks;   // by spot key
    // A city someone is in takes its next arrival from then.
    std::unordered_map<uint64_t, Clock::time_point>    g_cityNextFetch;
    std::unordered_set<uint64_t>   g_watched;      // (map << 32 | zone) with a real player in it, as of the last look

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    // City life (below): what Let, Settle, Stay and BringIn call of it.
    void PutAwayPole(Player* bot, Visit& v);
    void LifeSettle(Player* bot, PlayerbotAI* ai, Visit& v, Clock::time_point now);
    void RaiseDance(Player* bot, Unit* with);
    bool FlyIn(Player* bot, PlayerbotAI* ai, const PbpHangoutSpot& s, uint32_t zone, Clock::time_point now);
    bool FlyInOne(const PbpHangoutSpot& s, uint32_t zone, std::vector<Player*>& fetchable, Clock::time_point now);
    bool FlyOut(Player* bot, PlayerbotAI* ai);

    float Frand(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(Rng());
    }

    bool Roll(uint32_t percent)
    {
        return percent > 0 && std::uniform_int_distribution<uint32_t>(1, 100)(Rng()) <= percent;
    }

    PlayerbotAI* AiOf(Player* p)
    {
        return p ? PlayerbotsMgr::instance().GetPlayerbotAI(p) : nullptr;
    }

    PbpHangoutSpot const* SpotByKey(const std::string& key)
    {
        for (const PbpHangoutSpot& spot : g_HangoutSpots)
            if (spot.key == key)
                return &spot;
        return nullptr;
    }

    // The zone a spot is in (the Undercity, not the Ruins of Lordaeron over
    // it; Dalaran, not Crystalsong under it). 0 while its map is not up.
    uint32_t ZoneOf(const PbpHangoutSpot& s)
    {
        SpotZone& known = g_spotZones[s.key];
        if (known.zone && known.map == s.map && known.x == s.x && known.y == s.y)
            return known.zone;
        Map* map = sMapMgr->FindBaseMap(s.map);
        const uint32_t zone = map ? map->GetZoneId(PHASEMASK_NORMAL, s.x, s.y, s.z) : 0;
        known = SpotZone{ s.map, s.x, s.y, zone };
        return zone;
    }

    uint64_t WatchKey(uint32_t map, uint32_t zone)
    {
        return (uint64_t(map) << 32) | zone;
    }

    // A city whose side the bot is on: an Alliance capital takes Alliance
    // bots, a Horde one Horde bots, Shattrath and Dalaran anyone.
    bool TeamFits(Player* bot, uint32_t zone)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zone);
        if (!area)
            return false;
        if (area->team == AREATEAM_ALLY)
            return bot->GetTeamId() == TEAM_ALLIANCE;
        if (area->team == AREATEAM_HORDE)
            return bot->GetTeamId() == TEAM_HORDE;
        return true;
    }

    // Outland's and Northrend's cities are for the levels that go there:
    // Shattrath from 58, Dalaran from 68. (Silvermoon and the Exodar are on
    // the Outland map too, but they are starting cities.)
    uint32_t MinLevelFor(const PbpHangoutSpot& s, uint32_t zone)
    {
        constexpr uint32_t kSilvermoonCity = 3487, kTheExodar = 3557;
        uint32_t level = g_HangoutBringInMinLevel;
        if (s.map == 571)
            level = std::max<uint32_t>(level, 68);
        else if (s.map == 530 && zone != kSilvermoonCity && zone != kTheExodar)
            level = std::max<uint32_t>(level, 58);
        return level;
    }

    // The inns within Hangout.ArrivalRange of a spot, in its zone, nearest
    // first -- for a spot outside a city's gates (Duels.Spots), the city's
    // too: people come out through the gate. Empty until the spot's map is up.
    const std::vector<Inn>& InnsFor(const PbpHangoutSpot& s, uint32_t zone)
    {
        const bool anyZone = DuelSpot(s.key);
        SpotInns& known = g_spotInns[s.key];
        if (known.known && known.map == s.map && known.x == s.x && known.y == s.y && known.anyZone == anyZone)
            return known.inns;
        known = SpotInns{};
        Map* base = sMapMgr->FindBaseMap(s.map);
        if (!base || !zone)
            return known.inns;   // looked for again once it is up
        known.map     = s.map;
        known.x       = s.x;
        known.y       = s.y;
        known.anyZone = anyZone;
        known.known   = true;
        if (g_HangoutArrivalRange <= 0.0f)
            return known.inns;
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.mapid != s.map || std::fabs(data.posZ - s.z) > kMaxClimb)
                continue;
            const float dist = std::hypot(data.posX - s.x, data.posY - s.y);
            if (dist > g_HangoutArrivalRange)
                continue;
            CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(data.id);
            if (!info || !((info->npcflag | data.npcflag) & UNIT_NPC_FLAG_INNKEEPER))
                continue;
            if (!anyZone && base->GetZoneId(PHASEMASK_NORMAL, data.posX, data.posY, data.posZ) != zone)
                continue;
            Inn inn;
            inn.x       = data.posX;
            inn.y       = data.posY;
            inn.z       = data.posZ;
            inn.o       = data.orientation;
            inn.dist    = dist;
            inn.faction = info->faction;
            known.inns.push_back(inn);
        }
        std::sort(known.inns.begin(), known.inns.end(), [](const Inn& a, const Inn& b) { return a.dist < b.dist; });
        if (known.inns.empty())
            LOG_INFO("module.playerbotsplus",
                     "[Playerbots Plus] City hangouts: no inn within {:.0f} yards of {}; bots are fetched for it only "
                     "while nobody is in the city.",
                     g_HangoutArrivalRange, s.name);
        return known.inns;
    }

    // The nearest of a spot's inns that is friendly to this bot (else one
    // that is at least not hostile): nobody hearths into the other side's inn.
    Inn const* InnFor(Player* bot, const std::vector<Inn>& inns)
    {
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        if (!mine)
            return nullptr;
        Inn const* fallback = nullptr;
        for (const Inn& inn : inns)
        {
            FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(inn.faction);
            if (!theirs || mine->IsHostileTo(*theirs) || theirs->IsHostileTo(*mine))
                continue;
            if (mine->IsFriendlyTo(*theirs) && theirs->IsFriendlyTo(*mine))
                return &inn;
            if (!fallback)
                fallback = &inn;
        }
        return fallback;
    }

    // A real player anywhere near a place, on its map: they would see a bot
    // appear there, or vanish.
    bool RealPlayerNear(uint32_t map, float x, float y, float z, float range)
    {
        for (Player* p : sRandomPlayerbotMgr.GetPlayers())
            if (p && p->IsInWorld() && p->GetMapId() == map && PbpIsRealPlayer(p) &&
                p->GetExactDistSq(x, y, z) < range * range)
                return true;
        return false;
    }

    // Someone would see a bot appear at this spot: in its zone, or near
    // enough over the zone line (the gates, from inside the city).
    bool Watched(const PbpHangoutSpot& s, uint32_t zone)
    {
        return g_watched.contains(WatchKey(s.map, zone)) || RealPlayerNear(s.map, s.x, s.y, s.z, 250.0f);
    }

    // In the spot's zone -- or, for one outside a city's gates, near it from
    // inside the city.
    bool InSpotsZone(Player* bot, const PbpHangoutSpot& s, uint32_t zone)
    {
        return bot->GetZoneId() == zone ||
               (DuelSpot(s.key) && bot->GetExactDist2d(s.x, s.y) <= std::max(g_HangoutArrivalRange, 150.0f) + 50.0f);
    }

    // Two bots on one spot of ground would stand inside each other.
    // Someone's place already, at any spot: two spots side by side (the
    // middle of the Valley of Strength and the bank) do not give two bots
    // one place.
    bool Taken(uint32_t map, float x, float y, float minDist)
    {
        for (const auto& [guid, v] : g_visits)
            if (v.map == map && std::hypot(v.x - x, v.y - y) < minDist)
                return true;
        return false;
    }

    // Bots at a spot, settled or on their way (`but` not counted).
    uint32_t AtSpot(const std::string& key, uint64_t but = 0)
    {
        uint32_t n = 0;
        for (const auto& [guid, v] : g_visits)
            if (v.spotKey == key && guid != but)
                ++n;
        return n;
    }

    // Nowhere left at `key` with `held` there: no more for it for now.
    void FoundFull(const std::string& key, uint32_t held, const Clock::time_point now)
    {
        g_full[key] = Full{ held, now };
        ++g_looks[key].full;
        ++g_stats.foundFull;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} is full with {} there: the rest of its room goes "
                     "to other spots for now.", key, held);
    }

    // Every spot's room now: its share of the places, or -- found full --
    // what it held then, the rest of its room given to spots that can take
    // people, its own city's first (PbpShareFullRoom). A spot found full
    // opens again once fewer are there, or Hangout.FullMinutes on.
    void ShareRoom(std::unordered_map<std::string, uint32_t>& there, const Clock::time_point now)
    {
        for (auto it = g_full.begin(); it != g_full.end();)
        {
            const bool open = !SpotByKey(it->first) || there[it->first] < it->second.held ||
                              now - it->second.since >= std::chrono::minutes(g_HangoutFullMinutes);
            it = open ? g_full.erase(it) : std::next(it);
        }
        std::vector<PbpSpotRoom> rooms;
        for (const PbpHangoutSpot& s : g_HangoutSpots)
        {
            auto full = g_full.find(s.key);
            rooms.push_back({ PbpCityOf(s.name.empty() ? s.key : s.name), s.capacity, s.hot, full != g_full.end(),
                              full != g_full.end() ? full->second.held : 0 });
        }
        const std::vector<uint32_t> room = PbpShareFullRoom(rooms);
        g_room.clear();
        for (size_t i = 0; i < g_HangoutSpots.size(); ++i)
            g_room[g_HangoutSpots[i].key] = room[i];
    }

    uint32_t RoomAt(const PbpHangoutSpot& s)
    {
        auto it = g_room.find(s.key);
        return it == g_room.end() ? s.capacity : it->second;
    }

    // A place within the spot: on the ground near the middle's height, in
    // sight of the middle (so not behind a wall; a mailbox is no wall:
    // kSightChecks), outdoors if the middle is
    // (so not inside the inn off the Valley of Strength), out of the water
    // and any fire, not where another bot's place is or anyone stands -- and
    // one you can walk to from the middle, so not the top of a crate or a
    // market stall.
    bool PickPlace(Player* bot, const PbpHangoutSpot& s, float& outX, float& outY, float& outZ)
    {
        Map* map = bot->GetMap();
        const uint32_t phaseMask = bot->GetPhaseMask();
        Looks& look = g_looks[s.key];
        float groundZ = map->GetHeight(phaseMask, s.x, s.y, s.z + 3.0f, true, g_HangoutHeightTolerance + 6.0f);
        if (groundZ <= INVALID_HEIGHT)
            groundZ = s.z;
        PositionFullTerrainStatus middle;
        map->GetFullTerrainStatusForPosition(phaseMask, s.x, s.y, groundZ, bot->GetCollisionHeight(), middle);
        // Plenty of quick looks (most land on a roof, in a shop, out of
        // sight of the middle), and a few paths worked out for the best.
        int paths = 0;
        for (int tries = 0; tries < 48 && paths < 8; ++tries)
        {
            const float angle = Frand(0.0f, kTwoPi);
            const float dist  = s.radius * std::sqrt(Frand(0.0f, 1.0f));
            const float x = s.x + dist * std::cos(angle);
            const float y = s.y + dist * std::sin(angle);
            const float z = map->GetHeight(phaseMask, x, y, s.z + 3.0f, true, g_HangoutHeightTolerance + 6.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - s.z) > g_HangoutHeightTolerance)
            {
                ++look.ground;
                continue;
            }
            if (map->IsInWater(phaseMask, x, y, z, 2.0f))
            {
                ++look.water;
                continue;
            }
            if (!map->isInLineOfSight(s.x, s.y, s.z + 2.0f, x, y, z + 2.0f, phaseMask, kSightChecks,
                                      VMAP::ModelIgnoreFlags::Nothing))
            {
                ++look.sight;
                continue;
            }
            if (Taken(s.map, x, y, 2.5f))
            {
                ++look.taken;
                continue;
            }
            if (middle.outdoors)
            {
                PositionFullTerrainStatus there;
                map->GetFullTerrainStatusForPosition(phaseMask, x, y, z, bot->GetCollisionHeight(), there);
                if (!there.outdoors)
                {
                    ++look.roof;
                    continue;   // under a roof off an open square: a shop, the inn
                }
            }
            // Not in a campfire or a brazier, nor in a vendor or the next person.
            // (Room for a mount too: whoever stands here may mount up.)
            if (PbpFire_Burns(bot, x, y, z, 1.0f + PbpFire_MountRoom()))
            {
                ++look.fire;
                continue;
            }
            if (PbpSpace_Crowded(map, bot, x, y, z, 2.5f, bot))
            {
                ++look.crowded;
                continue;
            }

            ++paths;
            PathGenerator path(bot);
            path.CalculatePath(s.x, s.y, s.z, x, y, z, false);
            const uint32_t type = path.GetPathType();
            // Where the path really ends: beside a crate, not on it.
            G3D::Vector3 const& end = path.GetActualEndPosition();
            if (!(type & PATHFIND_NORMAL) ||
                (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH)) ||
                std::hypot(end.x - x, end.y - y) > 1.0f || std::fabs(end.z - z) > 0.5f)
            {
                ++look.path;
                continue;
            }
            ++look.found;
            outX = x;
            outY = y;
            outZ = z;
            return true;
        }
        // Never a place at all: its middle is most likely inside something,
        // or not on the floor. Said once; .pbp hangouts has the counts.
        if (!look.said && !look.found && look.Looked() >= kNobodyAfter)
        {
            look.said = true;
            LOG_WARN("module.playerbotsplus", "[Playerbots Plus] Nobody can stand at hangout spot {}: no place found "
                     "in {} looks ({} out of sight of its middle, {} not at its height, {} no way there from it). Is "
                     "its middle ({} {} {} {}) inside something, or off the floor?", s.key, look.Looked(), look.sight,
                     look.ground, look.path, s.map, s.x, s.y, s.z);
        }
        return false;
    }

    bool IsBench(GameObject* go)
    {
        if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_CHAIR || g_HangoutBenchWords.empty())
            return false;
        std::string name = go->GetName();
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (const std::string& word : g_HangoutBenchWords)
            if (!word.empty() && name.find(word) != std::string::npos)
                return true;
        return false;
    }

    // A free bench near the bot, in sight of the spot's middle: fewer of this
    // module's bots on it than it has seats.
    GameObject* FindBench(Player* bot, const Visit& v)
    {
        if (g_HangoutBenchRange <= 0.0f)
            return nullptr;
        std::list<GameObject*> objects;
        Acore::GameObjectInRangeCheck check(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                                            g_HangoutBenchRange);
        Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(bot, objects, check);
        Cell::VisitObjects(bot, searcher, g_HangoutBenchRange);

        std::vector<GameObject*> free;
        for (GameObject* go : objects)
        {
            if (!IsBench(go) || std::fabs(go->GetPositionZ() - v.cz) > g_HangoutHeightTolerance)
                continue;
            if (!bot->GetMap()->isInLineOfSight(v.cx, v.cy, v.cz + 2.0f, go->GetPositionX(), go->GetPositionY(),
                                                go->GetPositionZ() + 1.5f, bot->GetPhaseMask(), kSightChecks,
                                                VMAP::ModelIgnoreFlags::Nothing))
                continue;
            uint32_t seats = 1;
            if (GameObjectTemplate const* info = go->GetGOInfo())
                seats = std::max<uint32_t>(1, info->chair.slots);
            uint32_t used = 0;
            for (const auto& [guid, other] : g_visits)
                if (other.bench == go->GetGUID())
                    ++used;
            if (used < seats)
                free.push_back(go);
        }
        if (free.empty())
            return nullptr;
        return free[std::uniform_int_distribution<size_t>(0, free.size() - 1)(Rng())];
    }

    // On its mount at its place: only out in the open. Where the bot stands
    // now is not the question -- one walking in from the street to a place in
    // the bank rode in on its mount and stood at the counter on it.
    bool CanRide(Player* bot, PlayerbotAI* ai, const Visit& v)
    {
        if (bot->GetMapId() != v.map || bot->GetLevel() < sPlayerbotAIConfig.UseGroundMountAtMinLevel ||
            !ai->HasStrategy("mount", BOT_STATE_NON_COMBAT))
            return false;
        PositionFullTerrainStatus there;
        bot->GetMap()->GetFullTerrainStatusForPosition(bot->GetPhaseMask(), v.x, v.y, v.z, bot->GetCollisionHeight(),
                                                       there);
        return there.outdoors;
    }

    const char* Doing(const Visit& v, Player* bot)
    {
        if (v.act == Act::Fishing)
            return "fishing at the water's edge";
        if (v.act == Act::Dancing && v.danceJoined)
            return "dancing";
        switch (v.posture)
        {
            case Posture::Mounted: return bot->IsMounted() ? "sat on your mount" : "standing about";
            case Posture::Bench:   return "sitting on a bench";
            case Posture::Sit:     return "sitting on the ground";
            default:               return "standing about";
        }
    }

    // At the gates, whatever it is doing, it is there for the duels.
    std::string DoingAt(const Visit& v, Player* bot)
    {
        std::string doing = Doing(v, bot);
        if (DuelSpot(v.spotKey))
            doing += ", watching the duels";
        return doing;
    }

    // In a queue (a dungeon, a battleground): a proposal or an invite could
    // come at any moment, and its AI has to answer it.
    bool Queued(Player* bot)
    {
        return bot->InBattlegroundQueue() || sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE;
    }

    // Something wants the bot's own AI right now: an invite to answer (a
    // group, a guild, an arena team), a trade, a duel, a queue, a fight, a
    // party it is getting together for a group quest. A held AI would see
    // none of it.
    bool Wanted(Player* bot)
    {
        return !bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() || bot->GetGroupInvite() ||
               bot->GetTradeData() || bot->duel || bot->GetGuildIdInvited() || bot->GetArenaTeamIdInvited() ||
               Queued(bot) || PbpGroupQuests_Busy(bot);
    }

    // Done with it: whatever this changed goes back, and its AI runs again
    // now. `moveOn`: its time there is up, so it goes and does something else.
    void Let(Player* bot, PlayerbotAI* ai, Visit& v, bool moveOn, const Clock::time_point now)
    {
        if (!bot)
            return;
        // Not left dancing, or holding a fishing pole.
        if (v.act == Act::Dancing && bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
            bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
        PutAwayPole(bot, v);
        v.act = Act::None;
        if (v.afkSet && bot->isAFK())
            bot->ToggleAFK();
        v.afkSet = false;
        if (v.phase == Phase::Settled && (v.posture == Posture::Sit || v.posture == Posture::Bench) &&
            bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (ai)
        {
            if (v.holding || v.phase != Phase::Travel)
                ai->SetNextCheckDelay(0);
            if (moveOn)
            {
                NewRpgInfo& info = ai->rpgInfo;
                if (std::holds_alternative<NewRpgInfo::Rest>(info.data) ||
                    std::holds_alternative<NewRpgInfo::GoCamp>(info.data))
                    info.ChangeToIdle();
            }
        }
        v.holding = false;
        g_cooldown[bot->GetGUID().GetRawValue()] = now + std::chrono::minutes(g_HangoutCooldownMinutes);
    }

    // A fetched bot goes back where it came from once it is done, if nobody
    // would see it vanish here or turn up there: so a city nobody is in does
    // not fill up with bots that are done hanging out in it. False: it stays,
    // and goes about its business from here like anyone else.
    bool SendBack(Player* bot, PlayerbotAI* ai, const Visit& v)
    {
        if (!ai || !bot->IsInWorld() || bot->IsBeingTeleported() || Wanted(bot) || bot->GetVehicle() ||
            RealPlayerNear(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 150.0f) ||
            RealPlayerNear(v.fromMap, v.fromX, v.fromY, v.fromZ, 150.0f))
            return false;
        bot->GetMotionMaster()->Clear();
        ai->Reset(true);
        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        if (!bot->TeleportTo(v.fromMap, v.fromX, v.fromY, v.fromZ, v.fromO))
            return false;
        ++g_stats.sentBack;
        return true;
    }

    bool IdleInRpg(PlayerbotAI* ai)
    {
        const auto& data = ai->rpgInfo.data;
        return std::holds_alternative<NewRpgInfo::Idle>(data) || std::holds_alternative<NewRpgInfo::Rest>(data) ||
               std::holds_alternative<NewRpgInfo::WanderNpc>(data);
    }

    // `from`: a fetched bot that found no room at the spot it came for, sent
    // on to another in the city -- it goes back where it was fetched from
    // all the same, once done.
    bool Send(Player* bot, PlayerbotAI* ai, const PbpHangoutSpot& s, const Clock::time_point now,
              Visit const* from = nullptr)
    {
        Visit v;
        if (from && from->broughtIn)
        {
            v.broughtIn = true;
            v.fromMap   = from->fromMap;
            v.fromX     = from->fromX;
            v.fromY     = from->fromY;
            v.fromZ     = from->fromZ;
            v.fromO     = from->fromO;
        }
        v.spotKey  = s.key;
        v.spotName = s.name;
        v.map      = s.map;
        v.cx       = s.x;
        v.cy       = s.y;
        v.cz       = s.z;
        v.radius   = s.radius;
        v.started  = now;
        if (!PickPlace(bot, s, v.x, v.y, v.z))
            return false;   // nowhere that checks out just now: another time

        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        ai->rpgInfo.ChangeToGoCamp(WorldPosition(s.map, v.x, v.y, v.z));
        g_visits[bot->GetGUID().GetRawValue()] = std::move(v);
        ++g_stats.sent;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} heads over to hang out at {}.", bot->GetName(),
                     s.name);
        return true;
    }

    // ---- bots fetched from elsewhere (Hangout.BringIn) --------------------------

    bool StillFree(Player* bot, const Visit& v);
    void Arrived(Player* bot, PlayerbotAI* ai, Visit& v, Clock::time_point now);

    // A bot that may be fetched from wherever it is, for this spot: one of
    // playerbots' random bots, on its own, wanted by nobody, of the city's
    // side and of a level that goes there, with no real player near enough to
    // see it go. (The cheap checks -- alive, out of a fight, not grouped, not
    // in an instance, on a boat or a flight -- are made before.)
    bool CanBringIn(Player* bot, PlayerbotAI* ai, const PbpHangoutSpot& s, uint32_t zone)
    {
        if (!ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot) ||
            !ai->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) || Wanted(bot))
            return false;
        if (bot->GetLevel() < MinLevelFor(s, zone) || !TeamFits(bot, zone))
            return false;
        return !RealPlayerNear(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 150.0f);
    }

    // Where a bot fetched in at an inn turns up: a few steps in front of the
    // innkeeper, wherever there is floor in sight of it with nobody standing
    // there (and nobody else just turning up there), out of any fire.
    bool ArrivalPoint(Player* bot, Map* base, Inn const& inn, float& outX, float& outY, float& outZ)
    {
        for (int tries = 0; tries < 12; ++tries)
        {
            const float angle = inn.o + Frand(-1.3f, 1.3f);
            const float dist  = Frand(2.5f, 6.0f);
            const float x = inn.x + dist * std::cos(angle);
            const float y = inn.y + dist * std::sin(angle);
            const float z = base->GetHeight(PHASEMASK_NORMAL, x, y, inn.z + 2.0f, true, 6.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - inn.z) > 1.0f || base->IsInWater(PHASEMASK_NORMAL, x, y, z, 2.0f))
                continue;
            if (!base->isInLineOfSight(inn.x, inn.y, inn.z + 1.5f, x, y, z + 1.5f, PHASEMASK_NORMAL,
                                       LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                continue;
            bool landing = false;
            for (const auto& [guid, v] : g_visits)
                if (v.phase == Phase::Arriving && v.map == base->GetId() && std::hypot(v.x - x, v.y - y) < 2.5f)
                    landing = true;
            if (landing || PbpSpace_Crowded(base, bot, x, y, z, 2.5f, bot) ||
                PbpFire_BurnsAt(base, bot, bot->GetObjectSize(), x, y, z, 0.5f))
                continue;
            outX = x;
            outY = y;
            outZ = z;
            return true;
        }
        return false;
    }

    // Off it goes: to an inn in the city while someone is in it (it walks
    // over from there, like anyone who has just hearthed in), straight to the
    // spot while nobody is.
    bool BringIn(Player* bot, PlayerbotAI* ai, const PbpHangoutSpot& s, uint32_t zone, bool watched,
                 const Clock::time_point now)
    {
        float x = s.x, y = s.y, z = s.z, o = Frand(0.0f, kTwoPi);
        Map* base = sMapMgr->FindBaseMap(s.map);
        if (!base)
            return false;
        if (watched)
        {
            Inn const* inn = InnFor(bot, InnsFor(s, zone));
            if (!inn)
                return false;   // nowhere to come in that a player would not see it appear
            // Somewhere in front of the innkeeper where nobody stands: people
            // who hearth in turn up about the room, not inside one another.
            x = inn->x;
            y = inn->y;
            z = inn->z;
            o = inn->o;
            if (!ArrivalPoint(bot, base, *inn, x, y, z))
            {
                // A step out from behind the counter, if there is floor there.
                const float sx = inn->x + 1.5f * std::cos(inn->o), sy = inn->y + 1.5f * std::sin(inn->o);
                const float sz = base->GetHeight(PHASEMASK_NORMAL, sx, sy, inn->z + 2.0f, true, 6.0f);
                if (sz > INVALID_HEIGHT && std::fabs(sz - inn->z) < 0.6f)
                {
                    x = sx;
                    y = sy;
                    z = sz;
                }
            }
        }
        else
        {
            if (RealPlayerNear(s.map, x, y, z, 150.0f))
                return false;   // over the zone line from someone after all
            const float ground = base->GetHeight(PHASEMASK_NORMAL, x, y, z + 2.0f, true, 6.0f);
            if (ground > INVALID_HEIGHT && std::fabs(ground - z) < 3.0f)
                z = ground;
        }
        z += 0.05f;

        Visit v;
        v.spotKey   = s.key;
        v.spotName  = s.name;
        v.map       = s.map;
        v.cx        = s.x;
        v.cy        = s.y;
        v.cz        = s.z;
        v.radius    = s.radius;
        v.phase     = Phase::Arriving;
        v.started   = now;
        v.broughtIn = true;
        v.viaInn    = watched;
        v.fromMap   = bot->GetMapId();
        v.fromX     = bot->GetPositionX();
        v.fromY     = bot->GetPositionY();
        v.fromZ     = bot->GetPositionZ();
        v.fromO     = bot->GetOrientation();
        // Back on the ground (or the water) it was over, not in mid-air.
        const float floor = bot->GetMapWaterOrGroundLevel(v.fromX, v.fromY, v.fromZ);
        if (floor > INVALID_HEIGHT && v.fromZ - floor > 1.0f && v.fromZ - floor < 50.0f)
            v.fromZ = floor + 0.05f;

        // The way playerbots sends a random bot somewhere itself -- on foot
        // and at the keyboard, as anyone who has just hearthed in: not sat
        // on a horse in the inn, nor <AFK> from out where nobody was.
        bot->GetMotionMaster()->Clear();
        ai->Reset(true);
        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        if (bot->isAFK())
            bot->ToggleAFK();
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (!bot->TeleportTo(s.map, x, y, z, o))
            return false;
        v.x = x;   // where it lands, until it has its place: the next one lands elsewhere
        v.y = y;
        v.z = z;
        g_visits[bot->GetGUID().GetRawValue()] = std::move(v);
        g_fetchTimes.push_back(now);
        ++g_stats.broughtIn;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} is fetched to hang out at {} ({}).", bot->GetName(),
                     s.name, watched ? "at the inn" : "nobody about");
        return true;
    }

    // Landed: to its place, walking over from the inn while someone is in the
    // city, straight there while nobody is. False: it is not staying after all.
    bool Arrive(Player* bot, PlayerbotAI* ai, Visit& v, PbpHangoutSpot const& s, const Clock::time_point now)
    {
        const uint32_t zone = ZoneOf(s);
        if (bot->GetMapId() != v.map || !zone || !InSpotsZone(bot, s, zone) || !StillFree(bot, v))
        {
            ++g_stats.gaveUp;
            ++g_stats.lostElsewhere;
            Let(bot, ai, v, false, now);
            return false;
        }
        if (v.landedAt == Clock::time_point{})
        {
            v.landedAt = now;
            // Where people are now: playerbots' idea of whether it has
            // anything to do is taken again at once, not left as it was out
            // where nobody was -- that left it stood at the innkeeper, <AFK>
            // and doing nothing, for ten seconds or more.
            ai->AllowActivity(ALL_ACTIVITY, true);
            ai->SetNextCheckDelay(0);
            if (bot->isAFK())
                bot->ToggleAFK();
        }
        if (now - v.landedAt < std::chrono::seconds(1) || now < v.nextTry)
            return true;   // a moment to land, or to look again
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        if (!PickPlace(bot, s, px, py, pz))
        {
            v.nextTry = now + std::chrono::seconds(1);
            if (now - v.landedAt < std::chrono::seconds(6))
                return true;   // everywhere taken just now: another look in a moment
            const uint64_t raw = bot->GetGUID().GetRawValue();
            ++g_looks[v.spotKey].noRoom;
            FoundFull(v.spotKey, AtSpot(v.spotKey, raw), now);
            g_spotNextFetch[v.spotKey] = now + std::chrono::seconds(45);   // nowhere to put them: not for a while
            // Another spot in the city with room, the emptiest first: it walks
            // over there, as anyone finding one square packed tries the next.
            std::vector<std::pair<float, PbpHangoutSpot const*>> others;
            for (const PbpHangoutSpot& o : g_HangoutSpots)
            {
                if (o.key == s.key || o.map != s.map || ZoneOf(o) != zone || DuelSpot(o.key) ||
                    std::fabs(o.z - bot->GetPositionZ()) > kMaxClimb || bot->GetExactDist2d(o.x, o.y) > g_HangoutRecruitRange)
                    continue;
                const uint32_t room = RoomAt(o), at = AtSpot(o.key);
                if (room > at)
                    others.emplace_back(float(at) / float(room), &o);
            }
            std::sort(others.begin(), others.end(),
                      [](auto const& a, auto const& b) { return a.first < b.first; });
            const Visit from = v;   // Send replaces this bot's visit, v with it
            for (const auto& [fill, o] : others)
                if (Send(bot, ai, *o, now, &from))
                {
                    ++g_stats.redirected;
                    if (g_PbpDebug)
                        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} found no room at {}, and goes on to "
                                 "{}.", bot->GetName(), from.spotName, o->name);
                    return true;   // its new visit, on its way
                }
            ++g_stats.gaveUp;
            ++g_stats.lostNoRoom;
            Let(bot, ai, v, false, now);
            if (!SendBack(bot, ai, v))
            {
                // Not left stood at the innkeeper: out to the spot, to look
                // round it like anyone (playerbots' own "go camp").
                ai->rpgInfo.ChangeToGoCamp(WorldPosition(v.map, v.cx, v.cy, v.cz));
                if (g_PbpDebug)
                    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} found no room at {}, and wanders over "
                             "anyway.", bot->GetName(), v.spotName);
            }
            return false;
        }
        v.x = px;
        v.y = py;
        v.z = pz;
        if (v.viaInn)
        {
            // In at the inn: it walks over from there, like anyone who has just hearthed in.
            if (bot->IsSitState())
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            ai->rpgInfo.ChangeToGoCamp(WorldPosition(v.map, v.x, v.y, v.z));
            v.phase   = Phase::Travel;
            v.started = now;
            return true;
        }
        Arrived(bot, ai, v, now);
        return true;
    }

    // Spots short of bots (Hangout.BringInFill of their room) get one more
    // each, from elsewhere, up to Hangout.BringInPerMinute in all -- the
    // cities someone is in first.
    void BringInSome(const Clock::time_point now,
                     const std::vector<std::pair<PbpHangoutSpot const*, uint32_t>>& open,
                     std::vector<Player*>& fetchable)
    {
        while (!g_fetchTimes.empty() && now - g_fetchTimes.front() > std::chrono::seconds(60))
            g_fetchTimes.pop_front();
        if (g_fetchTimes.size() >= g_HangoutBringInPerMinute || fetchable.empty())
            return;
        uint32_t budget = g_HangoutBringInPerMinute - static_cast<uint32_t>(g_fetchTimes.size());

        std::unordered_map<std::string, uint32_t> there;
        for (const auto& [guid, v] : g_visits)
            ++there[v.spotKey];
        std::vector<std::pair<PbpHangoutSpot const*, uint32_t>> wanting, others;
        for (const auto& [s, zone] : open)
        {
            const uint32_t target = (RoomAt(*s) * g_HangoutBringInFill + 99) / 100;
            if (there[s->key] >= target)
                continue;
            auto next = g_spotNextFetch.find(s->key);
            if (next != g_spotNextFetch.end() && now < next->second)
                continue;
            (Watched(*s, zone) ? wanting : others).emplace_back(s, zone);
        }
        // The emptiest first: a city whose inn lets one in at a time fills
        // all its spots evenly, not whichever the conf happened to list
        // first (the order its keys come back in is no order at all).
        auto emptiest = [&there](std::vector<std::pair<PbpHangoutSpot const*, uint32_t>>& list)
        {
            std::shuffle(list.begin(), list.end(), Rng());
            std::stable_sort(list.begin(), list.end(), [&there](auto const& a, auto const& b)
            {
                return float(there[a.first->key]) / float(std::max<uint32_t>(1, RoomAt(*a.first))) <
                       float(there[b.first->key]) / float(std::max<uint32_t>(1, RoomAt(*b.first)));
            });
        };
        emptiest(wanting);
        emptiest(others);
        wanting.insert(wanting.end(), others.begin(), others.end());

        for (const auto& [s, zone] : wanting)
        {
            if (!budget || fetchable.empty())
                break;
            const bool watched = Watched(*s, zone);
            // Some come in by flight path, the way people come in, while
            // someone is about to see them land.
            if (watched && g_LifeEnable && Roll(g_LifeFlyInChance) && FlyInOne(*s, zone, fetchable, now))
            {
                --budget;
                continue;
            }
            if (watched && InnsFor(*s, zone).empty())
                continue;   // no inn to come in at: only while nobody is there
            if (watched)
            {
                auto next = g_cityNextFetch.find(WatchKey(s->map, zone));
                if (next != g_cityNextFetch.end() && now < next->second)
                    continue;   // someone just came in at the inn there
            }
            for (int tries = 0; tries < 12 && !fetchable.empty(); ++tries)
            {
                const size_t i = std::uniform_int_distribution<size_t>(0, fetchable.size() - 1)(Rng());
                Player* bot = fetchable[i];
                fetchable[i] = fetchable.back();
                fetchable.pop_back();
                if (g_visits.contains(bot->GetGUID().GetRawValue()))
                    continue;   // just sent to a spot in its own town
                PlayerbotAI* ai = AiOf(bot);
                if (!CanBringIn(bot, ai, *s, zone))
                    continue;
                if (BringIn(bot, ai, *s, zone, watched, now))
                {
                    --budget;
                    // Someone in the city sees them come in at its inns: not a
                    // stream of them, one every so often, whichever spot.
                    if (watched)
                        g_cityNextFetch[WatchKey(s->map, zone)] =
                            now + std::chrono::seconds(g_HangoutInnArrivalSeconds);
                    break;
                }
            }
        }
    }

    void Recruit(const Clock::time_point now)
    {
        if (g_HangoutSpots.empty())
            return;

        // Nobody online to see it: nothing to do.
        std::vector<Player*> real;
        for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
            if (p && p->IsInWorld() && PbpIsRealPlayer(p))
                real.push_back(p);
        g_watched.clear();
        for (Player* p : real)
            g_watched.insert(WatchKey(p->GetMapId(), p->GetZoneId()));
        if (real.empty())
            return;

        std::unordered_map<std::string, uint32_t> there;   // spot -> bots there or on the way
        for (const auto& [guid, v] : g_visits)
            ++there[v.spotKey];
        ShareRoom(there, now);
        std::vector<std::pair<PbpHangoutSpot const*, uint32_t>> open;   // spots with room, and their zone
        for (const PbpHangoutSpot& s : g_HangoutSpots)
            if (RoomAt(s) > there[s.key])
                if (const uint32_t zone = ZoneOf(s))
                    open.emplace_back(&s, zone);
        if (open.empty())
            return;

        const bool fetching = g_HangoutBringIn && g_HangoutBringInFill > 0 && g_HangoutBringInPerMinute > 0;
        std::vector<Player*> fetchable;   // could be fetched from where they are, as far as the cheap checks go
        std::unordered_map<std::string, std::vector<Player*>> willing;
        for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
        {
            if (!bot || !bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() ||
                bot->IsInFlight() || bot->GetVehicle() || bot->IsBeingTeleported() ||
                bot->InBattleground() || bot->InArena())
                continue;
            if (fetching && !bot->GetTransport() && bot->GetMap() && !bot->GetMap()->Instanceable() &&
                !bot->IsFlying() && !bot->HasUnitMovementFlag(MOVEMENTFLAG_FLYING) &&
                !g_visits.contains(bot->GetGUID().GetRawValue()))
            {
                auto cool = g_cooldown.find(bot->GetGUID().GetRawValue());
                if (cool == g_cooldown.end() || now >= cool->second)
                    fetchable.push_back(bot);
            }
            if (bot->isAFK())
                continue;   // (one fetched is no matter: that flag is playerbots', for a bot nobody is near)

            // The nearest spot with room, in this zone and on this level of it.
            PbpHangoutSpot const* best = nullptr;
            float bestDist = g_HangoutRecruitRange;
            for (const auto& [s, zone] : open)
            {
                if (s->map != bot->GetMapId() || (zone != bot->GetZoneId() && !DuelSpot(s->key)) ||
                    std::fabs(bot->GetPositionZ() - s->z) > kMaxClimb)
                    continue;
                const float d = bot->GetExactDist2d(s->x, s->y);
                if (d < bestDist)
                {
                    bestDist = d;
                    best = s;
                }
            }
            if (!best)
                continue;

            const uint64_t raw = bot->GetGUID().GetRawValue();
            if (g_visits.contains(raw))
                continue;
            auto cool = g_cooldown.find(raw);
            if (cool != g_cooldown.end() && now < cool->second)
                continue;
            PlayerbotAI* ai = AiOf(bot);
            // Wanted() covers a queue too: one waiting on a dungeon or a
            // battleground is not parked where it cannot answer the call.
            if (!ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot) ||
                !ai->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) || !IdleInRpg(ai) || Wanted(bot))
                continue;
            // Someone has it targeted: they may be about to talk to it.
            if (std::any_of(real.begin(), real.end(), [&](Player* p) { return p->GetTarget() == bot->GetGUID(); }))
                continue;
            willing[best->key].push_back(bot);
        }

        for (auto& [key, bots] : willing)
        {
            PbpHangoutSpot const* s = SpotByKey(key);
            if (!s || bots.empty() || !Roll(g_HangoutRecruitChance))
                continue;
            std::shuffle(bots.begin(), bots.end(), Rng());
            size_t tried = 0;
            bool sent = false;
            for (; tried < bots.size() && tried < 3 && !sent; ++tried)   // the first that has somewhere to stand
                sent = Send(bots[tried], AiOf(bots[tried]), *s, now);
            if (!sent && tried >= 2)
                FoundFull(key, there[key], now);   // twice nowhere to stand: full as it is
        }

        // Not enough idle bots in town: some from elsewhere.
        if (fetching)
            BringInSome(now, open, fetchable);
    }

    // Still a bot that may be hanging out at all.
    bool StillFree(Player* bot, const Visit& v)
    {
        return !Wanted(bot) && bot->GetMapId() == v.map && !bot->IsInFlight() && !bot->GetVehicle();
    }

    // On its way. False: it is not going there after all.
    bool Travel(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        const bool free = StillFree(bot, v);
        if (!free || now - v.started > std::chrono::seconds(g_HangoutTravelSeconds))
        {
            ++g_stats.gaveUp;
            ++(free ? g_stats.lostSlow : g_stats.lostElsewhere);
            Let(bot, ai, v, free, now);   // timed out: on with something else
            if (free && v.broughtIn)
                SendBack(bot, ai, v);     // (fetched, and stuck on the way: back where it was, if nobody sees)
            return false;
        }

        const float dist = bot->GetExactDist(v.x, v.y, v.z);
        bool there = false;
        const auto& data = ai->rpgInfo.data;
        if (auto* camp = std::get_if<NewRpgInfo::GoCamp>(&data))
        {
            const bool ours = std::fabs(camp->pos.GetPositionX() - v.x) < 0.5f &&
                              std::fabs(camp->pos.GetPositionY() - v.y) < 0.5f;
            if (!ours)
            {
                ++g_stats.gaveUp;
                ++g_stats.lostBusy;
                Let(bot, ai, v, false, now);
                return false;
            }
            there = dist < 10.0f;
        }
        else if (std::holds_alternative<NewRpgInfo::WanderNpc>(data) && dist < 14.0f)
            there = true;   // playerbots' "go camp" arrived, and turned into wandering between NPCs
        else
        {
            ++g_stats.gaveUp;   // something gave it other things to do
            ++g_stats.lostBusy;
            Let(bot, ai, v, false, now);
            return false;
        }
        if (there)
            Arrived(bot, ai, v, now);
        return true;
    }

    // Here (or in the city, for one fetched while nobody is about). It rests
    // for as long as it hangs out, its own AI held from now on, and how it
    // spends it is settled where it can see the place.
    void Arrived(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        ai->rpgInfo.ChangeToRest();
        ai->SetNextCheckDelay(kHoldMs);
        // Come out to fight someone: on its feet, at the keyboard, ready.
        GameObject* bench = !v.forDuel && Roll(g_HangoutBenchChance) ? FindBench(bot, v) : nullptr;
        if (bench)
        {
            v.posture = Posture::Bench;
            v.bench   = bench->GetGUID();
            v.bx      = bench->GetPositionX();
            v.by      = bench->GetPositionY();
            v.bz      = bench->GetPositionZ();
        }
        else if (!v.forDuel && CanRide(bot, ai, v) && Roll(g_HangoutMountedChance))
            v.posture = Posture::Mounted;
        else if (!v.forDuel && Roll(g_HangoutSitChance))
            v.posture = Posture::Sit;
        else
            v.posture = Posture::Stand;
        v.wantsAfk      = !v.forDuel && Roll(g_HangoutAfkChance);
        v.phase         = Phase::Approach;
        v.approachStart = now;
        if (v.posture == Posture::Bench)
            bot->GetMotionMaster()->MovePoint(kPointId, v.bx, v.by, v.bz);
        else
            bot->GetMotionMaster()->MovePoint(kPointId, v.x, v.y, v.z);
    }

    void Settle(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        if (v.posture != Posture::Mounted && bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);   // off the horse: it is sitting or standing here
        else if (v.posture == Posture::Mounted && !bot->IsMounted())
            // Up on its best mount now, by playerbots' own pick: a bot fetched
            // for a city nobody is in has an AI too idle to do it by itself.
            ai->DoSpecificAction("check mount state", Event(), true);

        if (v.posture == Posture::Bench)
        {
            GameObject* go = ObjectAccessor::GetGameObject(*bot, v.bench);
            if (go && go->isSpawned() && bot->GetExactDist2d(go) < 6.0f)
            {
                t_usingBench = true;
                go->Use(bot);   // the nearest free seat, the way the client does it
                t_usingBench = false;
            }
            if (!bot->IsSitState())
            {
                // Every seat taken after all: back to its own place, to sit there.
                v.posture       = Posture::Sit;
                v.bench         = ObjectGuid::Empty;
                v.approachStart = now;
                bot->GetMotionMaster()->MovePoint(kPointId, v.x, v.y, v.z);
                return;
            }
        }
        if (v.posture == Posture::Sit)
            bot->SetStandState(UNIT_STAND_STATE_SIT);
        v.seat = bot->getStandState();
        if (v.posture != Posture::Bench)
        {
            // Facing the middle, more or less: people stand about facing each other.
            const float angle = bot->GetExactDist2d(v.cx, v.cy) > 2.0f ? bot->GetAngle(v.cx, v.cy) + Frand(-0.9f, 0.9f)
                                                                     : Frand(0.0f, kTwoPi);
            bot->SetFacingTo(angle);
        }

        ai->rpgInfo.startT = getMSTime();
        v.phase     = Phase::Settled;
        v.settledAt = now;
        v.nextLook  = now + std::chrono::seconds(g_HangoutCheckSeconds);
        LifeSettle(bot, ai, v, now);
        if (v.until != Clock::time_point{})
            return;   // back from a duel, a step, an errand: the rest of its time here
        const uint32_t lo = g_HangoutStayMinutesMin * 60, hi = std::max(lo, g_HangoutStayMinutesMax * 60);
        v.until = now + std::chrono::seconds(std::uniform_int_distribution<uint32_t>(lo, hi)(Rng()));
        ++g_stats.settled;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} hangs out at {}, {}{}.", bot->GetName(), v.spotName,
                     DoingAt(v, bot), v.wantsAfk ? ", going AFK" : "");
    }

    void StartDuel(Player* bot, PlayerbotAI* ai, Visit& v);

    // The last steps, its AI held. False: something else came up.
    bool Approach(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        if (bot->duel && bot->IsAlive() && DuelSpot(v.spotKey))
        {
            StartDuel(bot, ai, v);   // challenged on its way to its place at the gates
            return true;
        }
        if (!StillFree(bot, v) || !std::holds_alternative<NewRpgInfo::Rest>(ai->rpgInfo.data))
        {
            ++g_stats.cutShort;
            Let(bot, ai, v, false, now);
            return false;
        }
        ai->SetNextCheckDelay(kHoldMs);
        ai->rpgInfo.startT = getMSTime();
        const bool  toBench = v.posture == Posture::Bench;
        const float reach   = toBench ? 3.0f : 1.5f;
        const auto  elapsed = now - v.approachStart;
        const bool  stopped = elapsed > std::chrono::seconds(2) && (!bot->movespline || bot->movespline->Finalized());
        const float dist    = toBench ? bot->GetExactDist2d(v.bx, v.by) : bot->GetExactDist2d(v.x, v.y);
        if (dist <= reach || stopped || elapsed > std::chrono::seconds(12))
            Settle(bot, ai, v, now);
        return true;
    }

    // A duel at the gates (Duels.Spots) is what it came for: its own AI
    // answers the challenge and fights it out, and it stays one of the crowd.
    void StartDuel(Player* bot, PlayerbotAI* ai, Visit& v)
    {
        if (v.afkSet && bot->isAFK())
            bot->ToggleAFK();
        v.afkSet  = false;
        v.holding = false;
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (v.posture == Posture::Bench)
        {
            v.posture = Posture::Stand;   // its seat is somebody else's by the time it is back
            v.bench   = ObjectGuid::Empty;
        }
        v.phase    = Phase::Duelling;
        v.duelOver = Clock::time_point{};
        ai->SetNextCheckDelay(0);
        ++g_stats.duels;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} has a duel at {}.", bot->GetName(), v.spotName);
    }

    // In a duel at the gates, or just out of one: its own AI fights it, then
    // eats or drinks if it likes, and once it has its breath back (or a
    // minute on) it goes back to its place in the crowd for the rest of its
    // time there. False: it is done here.
    bool Duel(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        // Something else its AI has to answer, or somewhere else altogether.
        if (!bot->IsAlive() || bot->GetGroup() || bot->GetGroupInvite() || bot->GetTradeData() ||
            bot->GetGuildIdInvited() || bot->GetArenaTeamIdInvited() || Queued(bot) || bot->GetMapId() != v.map ||
            bot->IsInFlight() || bot->GetVehicle())
        {
            ++g_stats.cutShort;
            Let(bot, ai, v, false, now);
            return false;
        }
        ai->rpgInfo.startT = getMSTime();   // still "resting" here, as far as playerbots' own plans go
        if (bot->duel)
        {
            v.duelOver = Clock::time_point{};
            return true;
        }
        if (v.duelOver == Clock::time_point{})
            v.duelOver = now;
        const auto since = now - v.duelOver;
        if (bot->IsInCombat())
        {
            if (since < std::chrono::seconds(10))
                return true;   // the duel's own fight, winding down
            ++g_stats.cutShort;   // a real fight: playerbots' business
            Let(bot, ai, v, false, now);
            return false;
        }
        if (bot->GetExactDist2d(v.cx, v.cy) > v.radius + 60.0f)
        {
            ++g_stats.cutShort;   // chased off somewhere: done here
            Let(bot, ai, v, false, now);
            return false;
        }
        // Sat eating or drinking, or still hurt: a minute at most.
        const bool hurt = bot->GetHealthPct() < 80.0f ||
                          (bot->getPowerType() == POWER_MANA && bot->GetPowerPct(POWER_MANA) < 60.0f);
        if ((hurt || bot->IsSitState()) && since < std::chrono::seconds(60))
            return true;
        if (v.until != Clock::time_point{} && now >= v.until)
        {
            ++g_stats.movedOn;
            Let(bot, ai, v, true, now);
            if (v.broughtIn)
                SendBack(bot, ai, v);
            return false;
        }
        // Back to its place, to stand, sit or get back on its mount there.
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (v.posture == Posture::Mounted && !CanRide(bot, ai, v))
            v.posture = Posture::Stand;
        ai->SetNextCheckDelay(kHoldMs);
        v.phase         = Phase::Approach;
        v.approachStart = now;
        bot->GetMotionMaster()->MovePoint(kPointId, v.x, v.y, v.z);
        return true;
    }

    // Every world tick once it is there: its AI held, so playerbots does not
    // stand it up, take its AFK off, or put it back on its horse. Let go at
    // once for anything its AI has to answer. False: it is done here.
    bool Hold(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        if (bot->duel && bot->IsAlive() && DuelSpot(v.spotKey))
        {
            StartDuel(bot, ai, v);
            return true;
        }
        if (Wanted(bot))
        {
            ++g_stats.cutShort;
            Let(bot, ai, v, false, now);
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} stops hanging out at {}: wanted.",
                         bot->GetName(), v.spotName);
            return false;
        }
        if (!v.holding)
        {
            // Mounted is left to playerbots, which puts an idle random bot on
            // its best mount; held once it is up there.
            if (v.posture == Posture::Mounted && !bot->IsMounted() && now - v.settledAt < kMountGrace)
                return true;
            v.holding = true;
            if (v.cameBack == Clock::time_point{})
            {
                if (v.wantsAfk)
                {
                    // (Already flagged: playerbots marks a bot nobody is near
                    // AFK. Ours now, to take off when it is done.)
                    if (!bot->isAFK())
                        bot->ToggleAFK();
                    v.afkSet = bot->isAFK();
                }
                else if (bot->isAFK())
                    bot->ToggleAFK();   // that flag, on one fetched from where nobody was: not AFK here
            }
        }
        else
        {
            // A world tick long enough for its AI to slip through once stands
            // it up and takes its AFK off: both go back.
            if ((v.posture == Posture::Sit || v.posture == Posture::Bench) && !bot->IsSitState() && !bot->isMoving() &&
                v.seat)
                bot->SetStandState(v.seat);
            if (v.afkSet && !bot->isAFK())
                bot->ToggleAFK();
        }
        ai->SetNextCheckDelay(kHoldMs);
        return true;
    }

    // Every Hangout.CheckSeconds while it is there. False: it is done here.
    bool Stay(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        const bool  onBench = v.posture == Posture::Bench;
        const float fromOwn = onBench ? bot->GetExactDist2d(v.bx, v.by) : bot->GetExactDist2d(v.x, v.y);
        const bool  moved   = bot->GetMapId() != v.map ||
                              (bot->GetExactDist2d(v.cx, v.cy) > v.radius + 15.0f && fromOwn > 10.0f);
        if (moved || !std::holds_alternative<NewRpgInfo::Rest>(ai->rpgInfo.data))
        {
            ++g_stats.cutShort;
            Let(bot, ai, v, false, now);
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} stops hanging out at {}: moved or busy.",
                         bot->GetName(), v.spotName);
            return false;
        }
        if (now >= v.until)
        {
            ++g_stats.movedOn;
            Let(bot, ai, v, true, now);
            const bool back = v.broughtIn && SendBack(bot, ai, v);
            // Off by flight path, some of them, the way people leave a city.
            const bool flies = !back && g_LifeEnable && Roll(g_LifeFlyOutChance) && FlyOut(bot, ai);
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} is done hanging out at {}{}.", bot->GetName(),
                         v.spotName, back ? ", and goes back where it came from" : flies ? ", and flies out" : "");
            return false;
        }
        ai->rpgInfo.startT = getMSTime();   // playerbots' rest is 30 seconds; it is still resting
        return true;
    }

    // ---- city life (PlayerbotsPlus.CityLife.*) ----------------------------------------
    //
    // A crowd that stands stock still for half an hour is a painting. People
    // in a city shift about, look round at whoever speaks or walks up, hop on
    // the spot, wander over to look at someone's rare mount, start a dance by
    // the bank, nip off to the mailbox or the auctioneer, fly in and out, and
    // fish in the canals. All of it only near a real player: nobody sees the
    // rest, and it costs nothing there.

    using PbpCityLife::Temper;

    std::vector<Player*> g_lifeWatchers;   // real players in the world, as of this look

    // A city, here: a zone with hangout spots in it.
    bool CityZone(uint32_t map, uint32_t zone)
    {
        for (const PbpHangoutSpot& s : g_HangoutSpots)
            if (s.map == map && ZoneOf(s) == zone)
                return true;
        return false;
    }

    Clock::duration Secs(uint32_t lo, uint32_t hi)
    {
        return std::chrono::seconds(PbpCityLife::Between(lo, hi, Frand(0.0f, 1.0f)));
    }

    bool NearWatcher(WorldObject const* who, float range)
    {
        for (Player* p : g_lifeWatchers)
            if (p->GetMapId() == who->GetMapId() && p->GetExactDistSq(who) <= range * range)
                return true;
        return false;
    }

    // Someone else's place, at any spot.
    bool TakenByOther(uint32_t map, float x, float y, float minDist, uint64_t self)
    {
        for (const auto& [guid, v] : g_visits)
            if (guid != self && v.map == map && std::hypot(v.x - x, v.y - y) < minDist)
                return true;
        return false;
    }

    void ScheduleFidget(Visit& v, const Clock::time_point now)
    {
        if (v.temper == Temper::Parked)
            v.nextFidget = now + std::chrono::hours(24);
        else if (v.temper == Temper::Restless)
            v.nextFidget = now + Secs(g_LifeRestlessMinSeconds, g_LifeRestlessMaxSeconds);
        else
            v.nextFidget = now + Secs(g_LifeShuffleMinSeconds, g_LifeShuffleMaxSeconds);
    }

    // Ground to stand on near (fromX, fromY, fromZ), `minDist`..`maxDist`
    // away at about `angle` (give or take half `spread`): on that floor, out
    // of the water and any fire, in sight of where it is measured from,
    // nobody's place and nobody in it, with a way to it -- and, for one on
    // its mount, out in the open. `inSpot`: inside the spot too.
    bool PlaceNear(Player* bot, const Visit& v, float fromX, float fromY, float fromZ, float minDist, float maxDist,
                   float angle, float spread, bool inSpot, float& outX, float& outY, float& outZ)
    {
        Map* map = bot->GetMap();
        const uint32_t phase = bot->GetPhaseMask();
        const uint64_t self = bot->GetGUID().GetRawValue();
        for (int tries = 0; tries < 10; ++tries)
        {
            const float a = angle + Frand(-spread / 2.0f, spread / 2.0f);
            const float d = Frand(minDist, maxDist);
            const float x = fromX + d * std::cos(a);
            const float y = fromY + d * std::sin(a);
            if (inSpot && std::hypot(x - v.cx, y - v.cy) > v.radius + 3.0f)
                continue;
            const float z = map->GetHeight(phase, x, y, fromZ + 2.0f, true, 4.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - fromZ) > 1.5f || map->IsInWater(phase, x, y, z, 2.0f))
                continue;
            if (!map->isInLineOfSight(fromX, fromY, fromZ + 1.5f, x, y, z + 1.5f, phase, kSightChecks,
                                      VMAP::ModelIgnoreFlags::Nothing))
                continue;
            if (TakenByOther(v.map, x, y, 2.0f, self) || PbpSpace_Crowded(map, bot, x, y, z, 2.0f, bot) ||
                PbpFire_Burns(bot, x, y, z, 1.0f + (v.posture == Posture::Mounted ? PbpFire_MountRoom() : 0.0f)))
                continue;
            if (v.posture == Posture::Mounted)
            {
                PositionFullTerrainStatus there;
                map->GetFullTerrainStatusForPosition(phase, x, y, z, bot->GetCollisionHeight(), there);
                if (!there.outdoors)
                    continue;
            }
            PathGenerator path(bot);
            path.CalculatePath(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), x, y, z, false);
            G3D::Vector3 const& end = path.GetActualEndPosition();
            if (!(path.GetPathType() & PATHFIND_NORMAL) ||
                (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT)) ||
                std::hypot(end.x - x, end.y - y) > 1.0f)
                continue;
            outX = x;
            outY = y;
            outZ = z;
            return true;
        }
        return false;
    }

    // To a new place -- a step, or over to someone -- as it was: on its mount
    // if it was, sat down again there if it was. Its own AI stays held.
    void Shift(Player* bot, PlayerbotAI* ai, Visit& v, float x, float y, float z, bool walk,
               const Clock::time_point now)
    {
        v.x = x;
        v.y = y;
        v.z = z;
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (bot->GetUInt32Value(UNIT_NPC_EMOTESTATE))
            bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);   // nobody slides along dancing
        v.phase         = Phase::Approach;
        v.approachStart = now;
        ai->SetNextCheckDelay(kHoldMs);
        bot->GetMotionMaster()->MovePoint(kPointId, x, y, z, walk ? FORCED_MOVEMENT_WALK : FORCED_MOVEMENT_NONE);
    }

    // ---- fishing ------------------------------------------------------------------------

    constexpr uint32_t kFishingPole = 6256;

    // Fishing, the best rank it knows (Grand Master down to Apprentice). 0: none.
    uint32_t FishingSpell(Player* bot)
    {
        for (uint32_t id : { 51294u, 33095u, 18248u, 7732u, 7731u, 7620u })
            if (bot->HasSpell(id))
                return id;
        return 0;
    }

    bool IsPole(Item* item)
    {
        ItemTemplate const* proto = item ? item->GetTemplate() : nullptr;
        return proto && proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
    }

    // The game's own way to put something on: what the client sends.
    void EquipTo(Player* bot, Item* item, uint8_t slot)
    {
        WorldPacket packet(CMSG_AUTOEQUIP_ITEM_SLOT, 9);
        packet << item->GetGUID() << slot;
        WorldPackets::Item::AutoEquipItemSlot equip(std::move(packet));
        equip.Read();
        bot->GetSession()->HandleAutoEquipItemSlotOpcode(equip);
    }

    // A pole in its hands: one from its bags, or a plain one (a random bot
    // gets one the way playerbots gives it one). What it held goes back
    // after (PutAwayPole). False: it could not.
    bool TakeOutPole(Player* bot, Visit& v)
    {
        Item* main = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
        if (IsPole(main))
            return true;
        auto findPole = [bot]() -> Item*
        {
            for (uint8_t slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot); IsPole(item))
                    return item;
            for (uint8_t bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
                if (Bag* b = bot->GetBagByPos(bag))
                    for (uint32_t j = 0; j < b->GetBagSize(); ++j)
                        if (Item* item = b->GetItemByPos(j); IsPole(item))
                            return item;
            return nullptr;
        };
        Item* pole = findPole();
        if (!pole && bot->StoreNewItemInBestSlots(kFishingPole, 1))
            pole = findPole();
        if (!pole)
            return false;   // no room in its bags for one
        Item* off = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
        v.oldMain = main ? main->GetGUID() : ObjectGuid::Empty;
        v.oldOff  = off ? off->GetGUID() : ObjectGuid::Empty;
        EquipTo(bot, pole, EQUIPMENT_SLOT_MAINHAND);
        v.poleOn = IsPole(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND));
        return v.poleOn;
    }

    // Its own weapons back in its hands, as it had them.
    void PutAwayPole(Player* bot, Visit& v)
    {
        if (!v.poleOn)
            return;
        v.poleOn = false;
        if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            bot->InterruptNonMeleeSpells(false);
        if (Item* main = v.oldMain ? bot->GetItemByGuid(v.oldMain) : nullptr; main && !main->IsEquipped())
            EquipTo(bot, main, EQUIPMENT_SLOT_MAINHAND);
        if (Item* off = v.oldOff ? bot->GetItemByGuid(v.oldOff) : nullptr; off && !off->IsEquipped())
            EquipTo(bot, off, EQUIPMENT_SLOT_OFFHAND);
        v.oldMain.Clear();
        v.oldOff.Clear();
    }

    // Where people fish in a city: ground at a water's edge near a spot,
    // looking out over water 10-20 yards off -- where a cast line lands --
    // deep enough to fish, in sight. Found from the map's own water, once per
    // spot, while someone is near enough for it to be loaded.
    struct FishPlace
    {
        uint32_t map = 0;
        float    x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;
    };
    std::unordered_map<std::string, std::vector<FishPlace>> g_fishPlaces;   // by spot key, once looked

    bool Fishable(Map* map, float x, float y, float standZ)
    {
        constexpr uint8_t kFishWater = MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN | MAP_LIQUID_TYPE_SLIME;
        LiquidData const liquid = map->GetLiquidData(PHASEMASK_NORMAL, x, y, standZ + 2.0f, 2.0f, kFishWater);
        if (liquid.Status == LIQUID_MAP_NO_WATER || liquid.Level <= INVALID_HEIGHT)
            return false;
        return liquid.Level - liquid.DepthLevel >= 1.0f && liquid.Level < standZ + 1.0f && liquid.Level > standZ - 12.0f;
    }

    const std::vector<FishPlace>& FishPlacesAt(const PbpHangoutSpot& s, Player* near)
    {
        auto it = g_fishPlaces.find(s.key);
        if (it != g_fishPlaces.end())
            return it->second;
        std::vector<FishPlace>& found = g_fishPlaces[s.key];
        Map* map = near->GetMap();
        if (!g_LifeFish || !g_LifeFishPlaces || !map || map->GetId() != s.map)
            return found;
        const float reach = s.radius + 30.0f;
        std::vector<FishPlace> edges;
        for (float dx = -reach; dx <= reach; dx += 5.0f)
            for (float dy = -reach; dy <= reach; dy += 5.0f)
            {
                const float x = s.x + dx, y = s.y + dy;
                if (std::hypot(dx, dy) > reach)
                    continue;
                const float z = map->GetHeight(PHASEMASK_NORMAL, x, y, s.z + 5.0f, true, 12.0f);
                if (z <= INVALID_HEIGHT || std::fabs(z - s.z) > 12.0f ||
                    map->IsInWater(PHASEMASK_NORMAL, x, y, z, 2.0f))
                    continue;
                float bestO = 0.0f;
                int best = 0;
                for (int d = 0; d < 12; ++d)
                {
                    const float o = kTwoPi * float(d) / 12.0f;
                    if (!Fishable(map, x + 15.0f * std::cos(o), y + 15.0f * std::sin(o), z))
                        continue;   // the middle of where a line lands: dry, and so the rest most likely
                    int wet = 1;
                    for (float r : { 11.0f, 18.0f, 20.0f })
                        wet += Fishable(map, x + r * std::cos(o), y + r * std::sin(o), z) ? 1 : 0;
                    if (wet > best)
                    {
                        best = wet;
                        bestO = o;
                    }
                }
                if (best < 4)
                    continue;
                const float wx = x + 15.0f * std::cos(bestO), wy = y + 15.0f * std::sin(bestO);
                LiquidData const surface = map->GetLiquidData(PHASEMASK_NORMAL, wx, wy, z + 2.0f, 2.0f, {});
                if (!map->isInLineOfSight(x, y, z + 1.8f, wx, wy, surface.Level + 0.3f, PHASEMASK_NORMAL,
                                          LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                    continue;
                edges.push_back({ s.map, x, y, z, bestO });
            }
        // The nearest to the spot first, a few yards apart, one there is a way to.
        std::sort(edges.begin(), edges.end(), [&s](const FishPlace& a, const FishPlace& b)
                  { return std::hypot(a.x - s.x, a.y - s.y) < std::hypot(b.x - s.x, b.y - s.y); });
        for (const FishPlace& e : edges)
        {
            if (found.size() >= g_LifeFishPlaces)
                break;
            if (std::any_of(found.begin(), found.end(),
                            [&e](const FishPlace& f) { return std::hypot(f.x - e.x, f.y - e.y) < 6.0f; }))
                continue;
            PathGenerator path(near);
            path.CalculatePath(s.x, s.y, s.z, e.x, e.y, e.z, false);
            if (!(path.GetPathType() & PATHFIND_NORMAL) ||
                (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT)))
                continue;
            found.push_back(e);
        }
        if (!found.empty())
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] City life: {} place{} to fish from by {}.",
                     found.size(), found.size() == 1 ? "" : "s", s.name);
        return found;
    }

    // ---- errands ------------------------------------------------------------------------

    // A banker, an auctioneer or a mailbox in a city: where one stands at it,
    // and what one faces. Found from the spawns, once per zone.
    struct Service
    {
        uint32_t map = 0;
        float    x = 0.0f, y = 0.0f, z = 0.0f;   // the NPC or the mailbox
        float    o = 0.0f;                       // ...an NPC's facing
        bool     mailbox = false;
        uint32_t faction = 0;                    // an NPC's: the other side's are not used
    };
    std::unordered_map<uint32_t, std::vector<Service>> g_services;   // by zone, once looked

    const std::vector<Service>& ServicesIn(uint32_t mapId, uint32_t zone)
    {
        auto it = g_services.find(zone);
        if (it != g_services.end())
            return it->second;
        std::vector<Service>& found = g_services[zone];
        Map* base = sMapMgr->FindBaseMap(mapId);
        if (!base)
            return found;
        std::vector<PbpHangoutSpot const*> spots;   // the city's: a service far from all of them is not in it
        for (const PbpHangoutSpot& s : g_HangoutSpots)
            if (s.map == mapId && ZoneOf(s) == zone && !DuelSpot(s.key))
                spots.push_back(&s);
        auto inCity = [&](float x, float y, float z)
        {
            return std::any_of(spots.begin(), spots.end(), [&](PbpHangoutSpot const* s)
                               { return std::hypot(s->x - x, s->y - y) < 400.0f && std::fabs(s->z - z) < 40.0f; }) &&
                   base->GetZoneId(PHASEMASK_NORMAL, x, y, z) == zone;
        };
        if (spots.empty())
            return found;
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.mapid != mapId || !(data.phaseMask & PHASEMASK_NORMAL))
                continue;
            CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(data.id);
            if (!info || !((info->npcflag | data.npcflag) & (UNIT_NPC_FLAG_BANKER | UNIT_NPC_FLAG_AUCTIONEER)))
                continue;
            if (inCity(data.posX, data.posY, data.posZ))
                found.push_back({ mapId, data.posX, data.posY, data.posZ, data.orientation, false, info->faction });
        }
        for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
        {
            if (data.mapid != mapId || !(data.phaseMask & PHASEMASK_NORMAL))
                continue;
            GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(data.id);
            if (info && info->type == GAMEOBJECT_TYPE_MAILBOX && inCity(data.posX, data.posY, data.posZ))
                found.push_back({ mapId, data.posX, data.posY, data.posZ, data.orientation, true, 0 });
        }
        return found;
    }

    // Where to stand at it: before an NPC, as people line up at the counter;
    // any side of a mailbox.
    bool StandAt(Player* bot, const Service& s, float& x, float& y, float& z)
    {
        Map* map = bot->GetMap();
        for (int tries = 0; tries < 8; ++tries)
        {
            const float a = s.mailbox ? Frand(0.0f, kTwoPi) : s.o + Frand(-0.7f, 0.7f);
            const float d = s.mailbox ? Frand(1.6f, 2.4f) : Frand(2.2f, 3.4f);
            x = s.x + d * std::cos(a);
            y = s.y + d * std::sin(a);
            z = map->GetHeight(bot->GetPhaseMask(), x, y, s.z + 2.0f, true, 4.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - s.z) > 1.5f || map->IsInWater(bot->GetPhaseMask(), x, y, z, 2.0f))
                continue;
            if (!map->isInLineOfSight(s.x, s.y, s.z + 1.5f, x, y, z + 1.5f, bot->GetPhaseMask(), kSightChecks,
                                      VMAP::ModelIgnoreFlags::Nothing))
                continue;
            if (PbpSpace_Crowded(map, bot, x, y, z, 1.6f, bot))
                continue;
            return true;
        }
        return false;
    }

    // Off to one now: the bank, the auction house, the mailbox, a fair walk
    // away in the city. Its own AI walks it there (playerbots' "go camp").
    bool StartErrand(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        if (!g_LifeErrandChance || g_HangoutSpots.empty())
            return false;
        std::vector<std::pair<float, Service const*>> near;
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        for (const Service& s : ServicesIn(bot->GetMapId(), bot->GetZoneId()))
        {
            const float d = bot->GetExactDist2d(s.x, s.y);
            if (d < 15.0f || d > g_LifeErrandRange || std::fabs(s.z - bot->GetPositionZ()) > kMaxClimb)
                continue;
            if (s.faction && mine)
            {
                FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(s.faction);
                if (theirs && (mine->IsHostileTo(*theirs) || theirs->IsHostileTo(*mine)))
                    continue;
            }
            near.emplace_back(d, &s);
        }
        if (near.empty())
            return false;
        std::sort(near.begin(), near.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        Service const& to = *near[std::uniform_int_distribution<size_t>(0, std::min<size_t>(near.size(), 5) - 1)(Rng())]
                                 .second;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!StandAt(bot, to, x, y, z))
            return false;
        if (v.afkSet && bot->isAFK())
            bot->ToggleAFK();
        v.afkSet = false;
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        if (bot->GetUInt32Value(UNIT_NPC_EMOTESTATE))
            bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
        v.holding    = false;
        v.phase      = Phase::Errand;
        v.errandStep = 0;
        v.ex = x;
        v.ey = y;
        v.ez = z;
        v.efx = to.x;
        v.efy = to.y;
        v.errandSince = now;
        ai->SetNextCheckDelay(0);
        ai->rpgInfo.ChangeToGoCamp(WorldPosition(bot->GetMapId(), x, y, z));
        ++g_stats.errands;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} nips off to the {} from {}.", bot->GetName(),
                     to.mailbox ? "mailbox" : "counter", v.spotName);
        return true;
    }

    // The spot of a city nearest a place: where one done at the mailbox
    // settles, if it does not go back.
    PbpHangoutSpot const* SpotNear(uint32_t map, uint32_t zone, float x, float y, float z)
    {
        PbpHangoutSpot const* best = nullptr;
        float bestDist = 0.0f;
        for (const PbpHangoutSpot& s : g_HangoutSpots)
        {
            if (s.map != map || ZoneOf(s) != zone || DuelSpot(s.key) || std::fabs(s.z - z) > kMaxClimb)
                continue;
            const float d = std::hypot(s.x - x, s.y - y);
            if (!best || d < bestDist)
            {
                best = &s;
                bestDist = d;
            }
        }
        return best;
    }

    // On an errand. False: done with hanging out here.
    bool Errand(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        // Two and a half minutes to get there; once there, ErrandMaxSeconds at most.
        if (!StillFree(bot, v) || (v.errandStep == 0 && now - v.errandSince > std::chrono::seconds(150)))
        {
            ++g_stats.cutShort;
            Let(bot, ai, v, true, now);
            return false;
        }
        const float dist = bot->GetExactDist2d(v.ex, v.ey);
        if (v.errandStep == 0)
        {
            // Playerbots' "go camp" turns into wandering between NPCs within
            // ten yards of where it was going: there, as far as this goes.
            const auto& data = ai->rpgInfo.data;
            const auto* camp = std::get_if<NewRpgInfo::GoCamp>(&data);
            const bool ours = camp && std::fabs(camp->pos.GetPositionX() - v.ex) < 0.5f &&
                              std::fabs(camp->pos.GetPositionY() - v.ey) < 0.5f;
            const bool arrived = (ours && dist < 10.0f) ||
                                 (std::holds_alternative<NewRpgInfo::WanderNpc>(data) && dist < 14.0f);
            if (!ours && !arrived)
            {
                ++g_stats.cutShort;   // playerbots gave it something else to do
                Let(bot, ai, v, false, now);
                return false;
            }
            if (!arrived)
                return true;
            // At the counter, or the mailbox: it stops there, held, and turns to it.
            ai->rpgInfo.ChangeToRest();
            ai->SetNextCheckDelay(kHoldMs);
            bot->GetMotionMaster()->Clear();
            if (dist > 1.0f)
                bot->GetMotionMaster()->MovePoint(kPointId, v.ex, v.ey, v.ez);
            v.errandStep  = 1;
            v.errandUntil = now + Secs(g_LifeErrandMinSeconds, g_LifeErrandMaxSeconds);
            v.errandFaced = false;
            ++g_stats.errandsDone;
            return true;
        }
        ai->SetNextCheckDelay(kHoldMs);
        ai->rpgInfo.startT = getMSTime();
        if (!v.errandFaced && (!bot->movespline || bot->movespline->Finalized()))
        {
            bot->SetFacingTo(bot->GetAngle(v.efx, v.efy));
            v.errandFaced = true;
        }
        if (now < v.errandUntil)
            return true;
        // Done there. Its time here up: off about its business. Otherwise
        // back to its spot, or to the one nearest, the way people drift.
        if (v.until != Clock::time_point{} && now >= v.until)
        {
            ++g_stats.movedOn;
            Let(bot, ai, v, true, now);
            if (v.broughtIn)
                SendBack(bot, ai, v);
            return false;
        }
        PbpHangoutSpot const* to = Roll(g_LifeErrandStayChance)
                                       ? SpotNear(v.map, bot->GetZoneId(), bot->GetPositionX(), bot->GetPositionY(),
                                                  bot->GetPositionZ())
                                       : SpotByKey(v.spotKey);
        if (!to || (to->key != v.spotKey && RoomAt(*to) <= AtSpot(to->key)))
            to = SpotByKey(v.spotKey);
        const Visit from = v;
        const uint64_t key = bot->GetGUID().GetRawValue();
        ai->SetNextCheckDelay(0);   // its own AI walks it, as it walked here
        if (to && Send(bot, ai, *to, now, &from))
        {
            Visit& now2 = g_visits[key];
            now2.until = from.until;   // the rest of its time, not a fresh stay
            return true;
        }
        ++g_stats.movedOn;
        Let(bot, ai, v, true, now);
        return false;
    }

    // ---- flights ------------------------------------------------------------------------

    // The flight path a spot's city is on: the nearest node to it of the
    // bot's side, if that is in the city's zone (Darnassus's is over in
    // Rut'theran Village: nobody flies in to it).
    uint32_t CityNode(const PbpHangoutSpot& s, uint32_t zone, TeamId team)
    {
        const uint32_t node = sObjectMgr->GetNearestTaxiNode(s.x, s.y, s.z, s.map, team);
        TaxiNodesEntry const* entry = node ? sTaxiNodesStore.LookupEntry(node) : nullptr;
        Map* base = sMapMgr->FindBaseMap(s.map);
        if (!entry || !base || entry->map_id != s.map || std::hypot(entry->x - s.x, entry->y - s.y) > 700.0f ||
            base->GetZoneId(PHASEMASK_NORMAL, entry->x, entry->y, entry->z) != zone)
            return 0;
        return node;
    }

    // How far a bot fetched by flight path may be from its own flight master.
    constexpr float kFlyInReach = 250.0f;

    // Fetched by flight path: off to its own nearest flight master, and in to
    // the city's, the way people come in. Nobody sees it teleported anywhere.
    bool FlyIn(Player* bot, PlayerbotAI* ai, const PbpHangoutSpot& s, uint32_t zone, const Clock::time_point now)
    {
        TravelMgr::FlightMasterInfo const* from = sTravelMgr.GetNearestFlightMasterInfo(bot);
        if (!from || !from->taxiNodeId || bot->GetMapId() != s.map || bot->GetDistance(from->pos) > kFlyInReach)
            return false;
        const uint32_t to = CityNode(s, zone, bot->GetTeamId());
        if (!to || to == from->taxiNodeId)
            return false;
        std::vector<uint32_t> path = sTravelNodeMap.FindTaxiPath(from->taxiNodeId, to);
        if (path.size() < 2)
            return false;

        Visit v;
        v.spotKey   = s.key;
        v.spotName  = s.name;
        v.map       = s.map;
        v.cx        = s.x;
        v.cy        = s.y;
        v.cz        = s.z;
        v.radius    = s.radius;
        v.x         = s.x;
        v.y         = s.y;
        v.z         = s.z;
        v.phase     = Phase::Flying;
        v.started   = now;
        v.broughtIn = true;
        v.viaInn    = true;   // walks over from where it lands
        v.fromMap   = bot->GetMapId();
        v.fromX     = bot->GetPositionX();
        v.fromY     = bot->GetPositionY();
        v.fromZ     = bot->GetPositionZ();
        v.fromO     = bot->GetOrientation();
        if (bot->isAFK())
            bot->ToggleAFK();
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        ai->rpgInfo.ChangeToTravelFlight(from->templateEntry, from->pos, std::move(path));
        g_visits[bot->GetGUID().GetRawValue()] = std::move(v);
        g_fetchTimes.push_back(now);
        ++g_stats.broughtIn;
        ++g_stats.flewIn;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} flies in to hang out at {}.", bot->GetName(),
                     s.name);
        return true;
    }

    // One of `fetchable` that can fly in to `s` now: one of the city's side on
    // its continent, on its own near a flight master (most bots are nowhere
    // near one, so one picked at random seldom could). Out of `fetchable`
    // once it goes. False: none could.
    bool FlyInOne(const PbpHangoutSpot& s, uint32_t zone, std::vector<Player*>& fetchable, const Clock::time_point now)
    {
        const uint32_t nodeA = CityNode(s, zone, TEAM_ALLIANCE), nodeH = CityNode(s, zone, TEAM_HORDE);
        if ((!nodeA && !nodeH) || fetchable.empty())
            return false;   // the city has no flight master of its own (Darnassus's is over in Rut'theran)
        const size_t n = fetchable.size(), start = std::uniform_int_distribution<size_t>(0, n - 1)(Rng());
        for (size_t k = 0; k < n && k < 400; ++k)
        {
            const size_t i = (start + k) % n;
            Player* bot = fetchable[i];
            if (bot->GetMapId() != s.map || !(bot->GetTeamId() == TEAM_ALLIANCE ? nodeA : nodeH) ||
                bot->GetLevel() < MinLevelFor(s, zone) || !TeamFits(bot, zone) ||
                g_visits.contains(bot->GetGUID().GetRawValue()))
                continue;
            TravelMgr::FlightMasterInfo const* from = sTravelMgr.GetNearestFlightMasterInfo(bot);
            if (!from || bot->GetDistance(from->pos) > kFlyInReach)
                continue;
            PlayerbotAI* ai = AiOf(bot);
            if (!CanBringIn(bot, ai, s, zone) || !FlyIn(bot, ai, s, zone, now))
                continue;
            fetchable[i] = fetchable.back();
            fetchable.pop_back();
            return true;
        }
        return false;
    }

    // Flying in. Landed in the city: as one in at the inn, it walks over from
    // there. False: it never went, or went somewhere else.
    bool Fly(Player* bot, PlayerbotAI* ai, Visit& v, const PbpHangoutSpot& s, const Clock::time_point now)
    {
        if (now - v.started > std::chrono::minutes(10) || (!bot->IsInFlight() && Wanted(bot)))
        {
            ++g_stats.gaveUp;
            ++g_stats.lostLanding;
            Let(bot, ai, v, false, now);
            return false;
        }
        if (bot->IsInFlight())
        {
            v.flewAt = now;
            return true;
        }
        if (std::holds_alternative<NewRpgInfo::TravelFlight>(ai->rpgInfo.data))
            return true;   // on its way to its flight master, or just off it
        const uint32_t zone = ZoneOf(s);
        if (v.flewAt == Clock::time_point{} || bot->GetMapId() != v.map || !zone || bot->GetZoneId() != zone)
        {
            ++g_stats.gaveUp;   // never took off, or came down somewhere else
            ++g_stats.lostLanding;
            Let(bot, ai, v, false, now);
            return false;
        }
        ++g_stats.landed;
        v.phase    = Phase::Arriving;
        v.started  = now;
        v.landedAt = Clock::time_point{};
        return true;
    }

    // Off by flight path: to the city's flight master, and away to somewhere
    // for its level (playerbots' own pick, and its own walk and flight).
    bool FlyOut(Player* bot, PlayerbotAI* ai)
    {
        if (!ai || bot->GetVehicle() || !bot->IsAlive() || bot->IsInCombat())
            return false;
        TravelMgr::FlightMasterInfo const* info = sTravelMgr.GetNearestFlightMasterInfo(bot);
        if (!info || bot->GetDistance(info->pos) > 400.0f)
            return false;
        std::vector<std::vector<uint32>> paths = sTravelMgr.GetOptimalFlightDestinations(bot);
        if (paths.empty() || paths.front().size() < 2)
            return false;
        ai->rpgInfo.ChangeToTravelFlight(info->templateEntry, info->pos, std::move(paths.front()));
        ++g_stats.flewOut;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} heads for the flight master to fly out.",
                     bot->GetName());
        return true;
    }

    // ---- acts: over to look at a mount, dancing, gathered to talk, fishing -----------------

    // Done with what took it off its place: back to its place, as it was.
    void EndAct(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        if (v.act == Act::Dancing && bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
            bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
        if (v.act == Act::Fishing)
            PutAwayPole(bot, v);
        v.act = Act::None;
        v.actWith.Clear();
        ScheduleFidget(v, now);
        if (v.away)
        {
            v.away = false;
            if (v.phase == Phase::Settled && ai)
                Shift(bot, ai, v, v.hx, v.hy, v.hz, false, now);
            else
            {
                v.x = v.hx;
                v.y = v.hy;
                v.z = v.hz;
            }
        }
    }

    // Off its place for an act: back to it after (EndAct).
    void LeavePlace(Visit& v)
    {
        if (v.away)
            return;
        v.away = true;
        v.hx = v.x;
        v.hy = v.y;
        v.hz = v.z;
    }

    // Free for something to happen: settled, at its keyboard, on its feet or
    // its mount, not already about something, nobody talking with it.
    bool FreeForLife(Player* bot, const Visit& v, const Clock::time_point now)
    {
        return v.phase == Phase::Settled && v.act == Act::None && !v.forDuel && !v.afkSet && now >= v.busyUntil &&
               (v.posture == Posture::Stand || v.posture == Posture::Mounted) && bot->IsAlive() && !bot->IsInCombat() &&
               !bot->isMoving() && !bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    }

    // Over to `who`, `r` yards off at `angle` round them, for `act`: false if
    // there is nowhere to stand there.
    bool GoOverTo(Player* bot, PlayerbotAI* ai, Visit& v, Unit* who, float angle, float r, Act act,
                  Clock::time_point until, const Clock::time_point now)
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!PlaceNear(bot, v, who->GetPositionX(), who->GetPositionY(), who->GetPositionZ(), r - 0.6f, r + 0.6f, angle,
                       0.8f, false, x, y, z))
            return false;
        LeavePlace(v);
        v.act      = act;
        v.actWith  = who->GetGUID();
        v.actUntil = until;
        Shift(bot, ai, v, x, y, z, false, now);
        return true;
    }

    // Settled again (Settle): what it is about decides how it stands.
    void LifeSettle(Player* bot, PlayerbotAI* /*ai*/, Visit& v, const Clock::time_point now)
    {
        if (!v.lifeSet)
        {
            v.lifeSet = true;
            v.temper  = PbpCityLife::PickTemper(std::uniform_int_distribution<uint32_t>(1, 100)(Rng()),
                                                g_LifeParkedShare, g_LifeRestlessShare);
            ScheduleFidget(v, now);
            v.nextTurn = now + Secs(g_LifeTurnMinSeconds, g_LifeTurnMaxSeconds);
        }
        Unit* with = v.actWith ? ObjectAccessor::GetUnit(*bot, v.actWith) : nullptr;
        switch (v.act)
        {
            case Act::Gawking:
            case Act::Chatting:
                if (with)
                {
                    bot->SetFacingToObject(with);
                    if (v.act == Act::Gawking && Roll(40))
                    {
                        static const uint32_t kAdmire[] = { EMOTE_ONESHOT_POINT, EMOTE_ONESHOT_APPLAUD,
                                                            EMOTE_ONESHOT_EXCLAMATION, EMOTE_ONESHOT_CHEER };
                        bot->HandleEmoteCommand(kAdmire[std::uniform_int_distribution<int>(0, 3)(Rng())]);
                    }
                    if (v.act == Act::Chatting)
                        if (Player* p = with->ToPlayer(); p && p->GetExactDist(bot) < 6.0f && !p->isMoving())
                            p->SetFacingToObject(bot);
                }
                break;
            case Act::Dancing:
                bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_DANCE);
                if (with && with != bot)
                {
                    bot->SetFacingToObject(with);
                    if (v.danceJoined && !v.danceSaid)
                    {
                        v.danceSaid = true;   // over there and dancing: it joined them
                        ++g_stats.danced;
                        RaiseDance(bot, with);
                    }
                }
                break;
            case Act::Fishing:
                bot->SetFacingTo(v.fishO);
                v.nextCast = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(800, 2500)(Rng()));
                v.castFails = 0;
                break;
            default:
                break;
        }
    }

    // A hop: a step's worth up into the air and down again, the way people
    // jump on the spot while they wait.
    void Hop(Player* bot)
    {
        Map* map = bot->GetMap();
        const float a = Roll(60) ? bot->GetOrientation() : Frand(0.0f, kTwoPi);
        const float d = Frand(0.3f, 0.9f);
        float x = bot->GetPositionX() + d * std::cos(a);
        float y = bot->GetPositionY() + d * std::sin(a);
        float z = map->GetHeight(bot->GetPhaseMask(), x, y, bot->GetPositionZ() + 2.0f, true, 3.0f);
        if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > 0.6f ||
            PbpSpace_Crowded(map, bot, x, y, z, 1.2f, bot))
        {
            x = bot->GetPositionX();
            y = bot->GetPositionY();
            z = bot->GetPositionZ();
        }
        bot->GetMotionMaster()->MoveJump(x, y, z, PbpCityLife::HopSpeed(std::max(0.25f, d)), PbpCityLife::kHopRise);
    }

    // Its mount rears up, or roars: what pressing jump standing still on a
    // mount does. The game's own handler for it (CMSG_MOUNTSPECIAL_ANIM),
    // which shows it to everyone about.
    bool Rear(Player* bot)
    {
        if (!bot->IsMounted() || bot->isMoving() || !bot->GetSession())
            return false;
        WorldPacket nothing(CMSG_MOUNTSPECIAL_ANIM, 0);
        bot->GetSession()->HandleMountSpecialAnimOpcode(nothing);
        return true;
    }

    // Who reared their mount: a real player (the packet hook, PbpHangout_NoteRear)
    // or one hanging out, of its own accord -- never one doing it back, or a
    // crowd would answer itself for ever.
    std::mutex              g_rearMutex;
    std::vector<ObjectGuid> g_rearNotes;
    std::unordered_map<uint64_t, Clock::time_point> g_rearAnswered;   // who was last done back to, when

    void NoteRear(const ObjectGuid& who)
    {
        std::lock_guard<std::mutex> lock(g_rearMutex);
        if (g_rearNotes.size() < 64)
            g_rearNotes.push_back(who);
    }

    // Someone reared their mount: one or two of the mounted ones hanging out
    // near them do it back, a moment later -- now and then, and not every
    // time someone does it over and over.
    void RearBack(const ObjectGuid& who, const Clock::time_point now)
    {
        constexpr float kRearBackRange = 20.0f;
        if (!g_LifeRearBackChance)
            return;
        auto last = g_rearAnswered.find(who.GetRawValue());
        if (last != g_rearAnswered.end() && now - last->second < std::chrono::seconds(6))
            return;
        Player* rider = ObjectAccessor::FindConnectedPlayer(who);
        if (!rider || !rider->IsInWorld() || !rider->IsMounted())
            return;
        std::vector<std::pair<Visit*, Player*>> could;
        for (auto& [guid, v] : g_visits)
        {
            if (guid == who.GetRawValue() || v.phase != Phase::Settled || v.act != Act::None || v.afkSet ||
                v.forDuel || v.map != rider->GetMapId() || v.rearAt != Clock::time_point{} || now < v.busyUntil ||
                now - v.rearedAt < std::chrono::seconds(20) ||
                std::hypot(v.x - rider->GetPositionX(), v.y - rider->GetPositionY()) > kRearBackRange)
                continue;
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
            if (bot && bot->IsInWorld() && bot->IsMounted() && !bot->isMoving() &&
                bot->GetExactDist(rider) <= kRearBackRange && bot->IsWithinLOSInMap(rider))
                could.emplace_back(&v, bot);
        }
        std::shuffle(could.begin(), could.end(), Rng());
        uint32_t answered = 0;
        for (auto& [v, bot] : could)
        {
            if (answered >= 2)
                break;
            if (!Roll(g_LifeRearBackChance))
                continue;
            v->rearAt = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(600, 2200)(Rng()));
            v->rearTo = who;
            ++answered;
        }
        if (answered)
            g_rearAnswered[who.GetRawValue()] = now;
    }

    // Looks round: at someone stood close by, or just a little turn.
    void LookRound(Player* bot)
    {
        std::list<Player*> near;
        Acore::AnyPlayerInObjectRangeCheck check(bot, 6.0f);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(bot, near, check);
        Cell::VisitObjects(bot, searcher, 6.0f);
        near.remove(bot);
        if (!near.empty() && Roll(60))
        {
            auto it = near.begin();
            std::advance(it, std::uniform_int_distribution<size_t>(0, near.size() - 1)(Rng()));
            bot->SetFacingToObject(*it);
        }
        else
            bot->SetFacingTo(bot->GetOrientation() + (Roll(50) ? 1.0f : -1.0f) * Frand(0.3f, 1.1f));
        ++g_stats.turns;
    }

    // Fishing: a line out, and another once that one is in.
    void FishTick(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        if (now >= v.actUntil || !g_LifeFish)
        {
            EndAct(bot, ai, v, now);
            return;
        }
        if (bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
        {
            v.castFails = 0;
            v.nextCast  = now + Secs(3, 9);   // a moment after this one is in
            return;
        }
        if (now < v.nextCast)
            return;
        if (v.castFails >= 3 || (!v.poleOn && !TakeOutPole(bot, v)))
        {
            EndAct(bot, ai, v, now);   // it will not take: the water, the skill or the pole
            return;
        }
        const uint32_t spell = FishingSpell(bot);
        if (!spell)
        {
            EndAct(bot, ai, v, now);
            return;
        }
        bot->SetFacingTo(v.fishO);
        bot->CastSpell(bot, spell, false);
        ++v.castFails;   // cleared once the line is out
        ++g_stats.casts;
        v.nextCast = now + std::chrono::seconds(3);
    }

    // Off to fish, at a place near it with nobody fishing there. On foot:
    // nobody casts a line from the saddle (the game will not have it).
    bool StartFishing(Player* bot, PlayerbotAI* ai, Visit& v, const PbpHangoutSpot& s, const Clock::time_point now)
    {
        if (!g_LifeFish || !g_LifeFishChance || v.posture != Posture::Stand || bot->IsMounted() ||
            !bot->GetSkillValue(SKILL_FISHING) || !FishingSpell(bot))
            return false;
        for (const FishPlace& f : FishPlacesAt(s, bot))
        {
            if (f.map != bot->GetMapId() || bot->GetExactDist2d(f.x, f.y) > 70.0f)
                continue;
            if (TakenByOther(f.map, f.x, f.y, 3.0f, bot->GetGUID().GetRawValue()))
                continue;
            PathGenerator path(bot);
            path.CalculatePath(f.x, f.y, f.z, false);
            if (!(path.GetPathType() & PATHFIND_NORMAL) || (path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)))
                continue;
            LeavePlace(v);
            v.act      = Act::Fishing;
            v.actUntil = now + std::chrono::seconds(PbpCityLife::Between(g_LifeFishMinMinutes * 60,
                                                                           g_LifeFishMaxMinutes * 60, Frand(0.0f, 1.0f)));
            v.fishO    = f.o;
            Shift(bot, ai, v, f.x, f.y, f.z, false, now);
            ++g_stats.fished;
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} goes fishing by {}.", bot->GetName(), s.name);
            return true;
        }
        return false;
    }

    // Now and then, near a real player: a look round, a step, a hop or
    // three, an errand, a go at fishing.
    void Fidget(Player* bot, PlayerbotAI* ai, Visit& v, const PbpHangoutSpot& s, const Clock::time_point now)
    {
        if (v.hops)
        {
            if (now >= v.nextHop && !bot->isMoving())
            {
                // On a mount, jump standing still is a rear or a roar: one
                // after another, a little apart; the first for those near
                // to answer.
                const bool mounted = bot->IsMounted();
                if (!mounted)
                    Hop(bot);
                else if (Rear(bot))
                {
                    if (now - v.rearedAt > std::chrono::seconds(10))
                        NoteRear(bot->GetGUID());
                    v.rearedAt = now;
                }
                --v.hops;
                v.nextHop = now + std::chrono::milliseconds(mounted ? std::uniform_int_distribution<int>(1600, 2800)(Rng())
                                                                    : std::uniform_int_distribution<int>(900, 1700)(Rng()));
            }
            return;
        }
        if (now >= v.nextTurn)
        {
            v.nextTurn = now + Secs(g_LifeTurnMinSeconds, g_LifeTurnMaxSeconds);
            if (!bot->isMoving() && v.posture != Posture::Bench)
                LookRound(bot);
        }
        if (now < v.nextFidget)
            return;
        ScheduleFidget(v, now);
        if (!FreeForLife(bot, v, now))
            return;
        // Back where it belongs first, if a hop or a push left it off its place.
        if (bot->GetExactDist2d(v.x, v.y) > 2.0f)
        {
            Shift(bot, ai, v, v.x, v.y, v.z, true, now);
            return;
        }
        const uint32_t roll = std::uniform_int_distribution<uint32_t>(1, 100)(Rng());
        if (roll <= g_LifeErrandChance && StartErrand(bot, ai, v, now))
            return;
        if (roll > g_LifeErrandChance && roll <= g_LifeErrandChance + g_LifeFishChance &&
            StartFishing(bot, ai, v, s, now))
            return;
        // A hop or three on foot; on a mount, a rear or three.
        const bool mounted = bot->IsMounted();
        if (Roll(mounted ? g_LifeRearChance : g_LifeJumpChance))
        {
            v.hops    = std::uniform_int_distribution<uint32_t>(1, 3)(Rng());
            v.nextHop = now;
            ++(mounted ? g_stats.rears : g_stats.hops);
            return;
        }
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (PlaceNear(bot, v, v.x, v.y, v.z, g_LifeStepMin, g_LifeStepMax, Frand(0.0f, kTwoPi), kTwoPi, true, x, y, z))
        {
            Shift(bot, ai, v, x, y, z, Roll(g_LifeWalkChance), now);
            ++g_stats.steps;
        }
    }

    // ---- rare mounts --------------------------------------------------------------------

    std::vector<bool>                                  g_mountSpells;    // by spell id: one that mounts you
    std::unordered_map<uint32_t, uint32_t>             g_mountOwners;    // spell -> how many online have it
    Clock::time_point                                  g_nextCensus{};
    std::unordered_map<uint64_t, uint32_t>             g_lastMount;      // player -> the mount it was on at the last look
    std::unordered_map<uint64_t, Clock::time_point>    g_gawkedAt;       // rider -> looked at then

    // Who has which mount, of everyone online: what makes one rare here.
    void Census(const Clock::time_point now)
    {
        if (now < g_nextCensus)
            return;
        g_nextCensus = now + std::chrono::minutes(10);
        if (g_mountSpells.empty())
        {
            // A flat table: every spell every player online knows is looked up in it.
            g_mountSpells.assign(sSpellMgr->GetSpellInfoStoreSize(), false);
            for (uint32_t id = 0; id < g_mountSpells.size(); ++id)
                if (SpellInfo const* info = sSpellMgr->GetSpellInfo(id); info && info->HasAura(SPELL_AURA_MOUNTED))
                    g_mountSpells[id] = true;
        }
        g_mountOwners.clear();
        for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
        {
            if (!p || !p->IsInWorld())
                continue;
            for (auto const& [spell, known] : p->GetSpellMap())
                if (spell < g_mountSpells.size() && g_mountSpells[spell] && known && known->State != PLAYERSPELL_REMOVED)
                    ++g_mountOwners[spell];
        }
    }

    uint32_t MountOf(Unit* who)
    {
        Unit::AuraEffectList const& mounts = who->GetAuraEffectsByType(SPELL_AURA_MOUNTED);
        return mounts.empty() ? 0 : mounts.front()->GetId();
    }

    // A few of those hanging out near `rider` come over to look at its rare mount.
    void Gawk(Player* rider, uint32_t spell, uint32_t owners, const Clock::time_point now)
    {
        std::vector<std::pair<float, uint64_t>> near;
        for (auto& [guid, v] : g_visits)
        {
            if (guid == rider->GetGUID().GetRawValue() || v.map != rider->GetMapId())
                continue;
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
            if (!bot || !bot->IsInWorld() || !FreeForLife(bot, v, now))
                continue;
            const float d = bot->GetExactDist(rider);
            if (d > g_LifeGawkRange || !bot->IsWithinLOSInMap(rider))
                continue;
            near.emplace_back(d + Frand(0.0f, 8.0f), guid);
        }
        if (near.empty())
            return;
        std::sort(near.begin(), near.end());
        const uint32_t want = std::min<uint32_t>(uint32_t(near.size()),
                                                 std::uniform_int_distribution<uint32_t>(g_LifeGawkMin, g_LifeGawkMax)(Rng()));
        Player* first = ObjectAccessor::FindConnectedPlayer(ObjectGuid(near.front().second));
        const float start = first ? rider->GetAngle(first) : Frand(0.0f, kTwoPi);
        std::vector<float> jitter;
        for (uint32_t i = 0; i < want; ++i)
            jitter.push_back(Frand(-1.0f, 1.0f));
        const std::vector<float> angles = PbpCityLife::RingAngles(want, start, jitter);
        Player* speaker = nullptr;
        uint32_t went = 0;
        for (uint32_t i = 0; i < want; ++i)
        {
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(near[i].second));
            auto it = g_visits.find(near[i].second);
            PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
            if (!bot || !ai || it == g_visits.end())
                continue;
            const auto until = now + Secs(g_LifeGawkMinSeconds, g_LifeGawkMaxSeconds);
            if (!GoOverTo(bot, ai, it->second, rider, angles[i], Frand(3.5f, 5.0f), Act::Gawking, until, now))
                continue;
            ++went;
            if (!speaker)
                speaker = bot;
        }
        if (!went)
            return;
        g_gawkedAt[rider->GetGUID().GetRawValue()] = now;
        ++g_stats.gawks;
        g_stats.gawkers += went;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
        char const* name = info ? info->SpellName[sWorld->GetDefaultDbcLocale()] : nullptr;
        if (info && (!name || !*name))
            name = info->SpellName[LOCALE_enUS];
        const std::string mount = name ? name : std::string();
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} came over to look at {}'s {} ({}).", went,
                     rider->GetName(), mount, PbpCityLife::RareText(owners));
        if (speaker && !mount.empty())
        {
            PlayerbotsPlus::BotEvent e;
            e.type   = PlayerbotsPlus::BotEventType::CityMountShown;
            e.bot    = speaker->GetGUID();
            e.other  = rider->GetGUID();
            e.what   = mount;
            e.detail = PbpCityLife::RareText(owners);
            e.have   = went;
            e.zone   = PbpZoneName(speaker);
            PbpRaiseBotEvent(e);
        }
    }

    // Who has just got on a rare mount near a real player in a city.
    void LookAtMounts(const Clock::time_point now)
    {
        if (!g_LifeGawk || !g_LifeGawkRareOwners)
            return;
        Census(now);
        std::unordered_set<uint64_t> seen;
        for (Player* real : g_lifeWatchers)
        {
            if (!CityZone(real->GetMapId(), real->GetZoneId()))
                continue;
            std::list<Player*> near;
            Acore::AnyPlayerInObjectRangeCheck check(real, 40.0f);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(real, near, check);
            Cell::VisitObjects(real, searcher, 40.0f);
            for (Player* p : near)
            {
                const uint64_t key = p->GetGUID().GetRawValue();
                if (!seen.insert(key).second)
                    continue;
                // Getting on, seen: one first seen already up there got on
                // a while ago, out of sight.
                const uint32_t spell = p->IsMounted() ? MountOf(p) : 0;
                auto [was, first] = g_lastMount.try_emplace(key, spell);
                const bool justUp = !first && spell && spell != was->second;
                was->second = spell;
                if (!justUp)
                    continue;
                auto owners = g_mountOwners.find(spell);
                if (owners == g_mountOwners.end() || !PbpCityLife::Rare(owners->second, g_LifeGawkRareOwners))
                    continue;
                if (auto at = g_gawkedAt.find(key);
                    at != g_gawkedAt.end() && now - at->second < std::chrono::minutes(g_LifeGawkCooldownMinutes))
                    continue;
                Gawk(p, spell, owners->second, now);
            }
        }
        if (g_lastMount.size() > 20000)
            g_lastMount.clear();
    }

    // ---- dance parties --------------------------------------------------------------------

    std::unordered_map<uint32_t, Clock::time_point> g_nextDance;    // zone -> when the next may start
    std::unordered_map<uint64_t, Clock::time_point> g_danceWith;    // real player dancing -> joined then

    void RaiseDance(Player* bot, Unit* with)
    {
        PlayerbotsPlus::BotEvent e;
        e.type = PlayerbotsPlus::BotEventType::CityDance;
        e.bot  = bot->GetGUID();
        if (with && with != bot)
            e.other = with->GetGUID();
        e.zone = PbpZoneName(bot);
        PbpRaiseBotEvent(e);
    }

    // Up to `count` of those hanging out near `dancer` join in, one after another.
    uint32_t JoinDance(Unit* dancer, uint32_t count, Clock::time_point until, const Clock::time_point now)
    {
        std::vector<std::pair<float, uint64_t>> near;
        for (auto& [guid, v] : g_visits)
        {
            if (guid == dancer->GetGUID().GetRawValue() || v.map != dancer->GetMapId())
                continue;
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
            if (!bot || !bot->IsInWorld() || !FreeForLife(bot, v, now) || v.posture == Posture::Mounted)
                continue;
            const float d = bot->GetExactDist(dancer);
            if (d < 15.0f && bot->IsWithinLOSInMap(dancer))
                near.emplace_back(d + Frand(0.0f, 5.0f), guid);
        }
        std::sort(near.begin(), near.end());
        uint32_t joined = 0;
        for (const auto& [d, guid] : near)
        {
            if (joined >= count)
                break;
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
            auto it = g_visits.find(guid);
            PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
            if (!bot || !ai || it == g_visits.end())
                continue;
            Visit& v = it->second;
            // Staggered: one joins, then another a few seconds on.
            v.danceJoinAt = now + std::chrono::seconds(2 + 5 * joined) +
                            std::chrono::milliseconds(std::uniform_int_distribution<int>(0, 3000)(Rng()));
            v.act      = Act::Dancing;
            v.actWith  = dancer->GetGUID();
            v.actUntil = until;
            v.danceJoined = false;
            v.danceSaid   = false;
            ++joined;
        }
        return joined;
    }

    void DanceParties(const Clock::time_point now)
    {
        if (!g_LifeDance)
            return;
        std::unordered_set<uint32_t> zones;
        for (Player* real : g_lifeWatchers)
        {
            if (!CityZone(real->GetMapId(), real->GetZoneId()))
                continue;
            zones.insert(real->GetZoneId());
            // Someone real dancing: one or two of those about join them.
            const uint64_t key = real->GetGUID().GetRawValue();
            if (real->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
            {
                if (!g_danceWith.contains(key))
                {
                    g_danceWith[key] = now;
                    if (Roll(g_LifeDanceWithChance))
                        JoinDance(real, std::uniform_int_distribution<uint32_t>(1, 2)(Rng()),
                                  now + std::chrono::minutes(2), now);
                }
            }
            else
                g_danceWith.erase(key);
        }
        for (uint32_t zone : zones)
        {
            auto [it, fresh] = g_nextDance.emplace(zone, Clock::time_point{});
            if (fresh)
                it->second = now + std::chrono::seconds(PbpCityLife::Between(g_LifeDanceMinutes * 30,
                                                                              g_LifeDanceMinutes * 90, Frand(0.0f, 1.0f)));
            if (now < it->second)
                continue;
            it->second = now + std::chrono::seconds(PbpCityLife::Between(g_LifeDanceMinutes * 30,
                                                                          g_LifeDanceMinutes * 90, Frand(0.0f, 1.0f)));
            // Someone hanging out near a real player there starts one.
            std::vector<uint64_t> could;
            for (auto& [guid, v] : g_visits)
            {
                if (v.temper == Temper::Parked)
                    continue;
                Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
                if (bot && bot->IsInWorld() && bot->GetZoneId() == zone && FreeForLife(bot, v, now) &&
                    v.posture == Posture::Stand && NearWatcher(bot, g_LifeRange * 0.6f))
                    could.push_back(guid);
            }
            if (could.empty())
                continue;
            const uint64_t pick = could[std::uniform_int_distribution<size_t>(0, could.size() - 1)(Rng())];
            Player* starter = ObjectAccessor::FindConnectedPlayer(ObjectGuid(pick));
            Visit& v = g_visits[pick];
            const auto until = now + Secs(g_LifeDanceMinSeconds, g_LifeDanceMaxSeconds);
            v.act         = Act::Dancing;
            v.actWith     = starter->GetGUID();
            v.actUntil    = until;
            v.danceJoined = true;
            v.danceSaid   = true;
            starter->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_STATE_DANCE);
            ++g_stats.dances;
            RaiseDance(starter, nullptr);
            const uint32_t joined =
                JoinDance(starter, std::uniform_int_distribution<uint32_t>(g_LifeDanceJoinMin, g_LifeDanceJoinMax)(Rng()),
                          until - std::chrono::seconds(5), now);
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} starts dancing at {}; {} to join in.",
                         starter->GetName(), v.spotName, joined);
        }
    }

    // One dancing (or about to join a dance): over to them when its turn
    // comes, then dancing till the music stops -- or the one it danced with
    // does.
    void DanceTick(Player* bot, PlayerbotAI* ai, Visit& v, const Clock::time_point now)
    {
        Unit* with = v.actWith ? ObjectAccessor::GetUnit(*bot, v.actWith) : nullptr;
        const bool own = with == bot;
        // Its own dance stopped by something else (an emote of its own, its
        // AI) is over too.
        if (now >= v.actUntil || !with || !with->IsInWorld() ||
            (v.danceJoined && bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) != EMOTE_STATE_DANCE) ||
            (!own && v.danceJoined && with->GetUInt32Value(UNIT_NPC_EMOTESTATE) != EMOTE_STATE_DANCE) ||
            (!own && with->GetExactDist(bot) > 20.0f))
        {
            EndAct(bot, ai, v, now);
            return;
        }
        if (v.danceJoined || now < v.danceJoinAt)
            return;
        // Its turn: over to them (a couple of yards off), dancing once there (LifeSettle).
        v.danceJoined = true;
        if (with->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE &&
            GoOverTo(bot, ai, v, with, with->GetAngle(bot) + Frand(-0.6f, 0.6f), Frand(2.2f, 3.4f), Act::Dancing,
                     v.actUntil, now))
            return;
        if (bot->GetExactDist(with) < 5.0f && with->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
        {
            LifeSettle(bot, ai, v, now);   // close enough as it is: dancing where it stands
            return;
        }
        EndAct(bot, ai, v, now);
    }

    // ---- every second ---------------------------------------------------------------------

    void Life(const Clock::time_point now)
    {
        g_lifeWatchers.clear();
        if (!g_LifeEnable)
            return;
        for (Player* p : sRandomPlayerbotMgr.GetPlayers())
            if (p && p->IsInWorld() && PbpIsRealPlayer(p))
                g_lifeWatchers.push_back(p);
        std::vector<ObjectGuid> reared;
        {
            std::lock_guard<std::mutex> lock(g_rearMutex);
            reared.swap(g_rearNotes);
        }
        for (const ObjectGuid& who : reared)
            RearBack(who, now);
        for (auto& [guid, v] : g_visits)
        {
            if (v.phase != Phase::Settled)
                continue;
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
            PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
            if (!bot || !ai || !bot->IsInWorld() || bot->IsBeingTeleported())
                continue;
            // Someone near reared their mount: it does it back, looking at them.
            if (v.rearAt != Clock::time_point{} && now >= v.rearAt)
            {
                v.rearAt = Clock::time_point{};
                Unit* who = ObjectAccessor::GetUnit(*bot, v.rearTo);
                if (who && v.act == Act::None && now >= v.busyUntil && bot->IsMounted() && !bot->isMoving() &&
                    who->GetExactDist(bot) < 25.0f)
                {
                    bot->SetFacingToObject(who);
                    if (Rear(bot))
                    {
                        v.rearedAt = now;
                        ++g_stats.rearedBack;
                    }
                }
            }
            // Someone spoke near it: it looks round at them.
            if (v.turnAt != Clock::time_point{} && now >= v.turnAt)
            {
                Unit* who = ObjectAccessor::GetUnit(*bot, v.turnTo);
                if (who && who->GetExactDist(bot) < 15.0f && !bot->isMoving() && v.posture != Posture::Bench)
                {
                    bot->SetFacingToObject(who);
                    ++g_stats.turns;
                }
                v.turnAt = Clock::time_point{};
            }
            switch (v.act)
            {
                case Act::Gawking:
                {
                    Unit* rider = ObjectAccessor::GetUnit(*bot, v.actWith);
                    if (now >= v.actUntil || !rider || !rider->IsMounted() || rider->GetExactDist(bot) > 12.0f)
                        EndAct(bot, ai, v, now);
                    continue;
                }
                case Act::Dancing:
                    DanceTick(bot, ai, v, now);
                    continue;
                case Act::Chatting:
                    if (now >= v.actUntil)
                        EndAct(bot, ai, v, now);
                    continue;
                case Act::Fishing:
                    FishTick(bot, ai, v, now);
                    continue;
                default:
                    break;
            }
            if (!v.afkSet && now >= v.busyUntil && !v.forDuel && NearWatcher(bot, g_LifeRange))
                if (PbpHangoutSpot const* spot = SpotByKey(v.spotKey))
                    Fidget(bot, ai, v, *spot, now);
        }
        static Clock::time_point nextMoments{};
        if (now < nextMoments || g_lifeWatchers.empty())
            return;
        nextMoments = now + std::chrono::seconds(2);
        LookAtMounts(now);
        DanceParties(now);
        for (auto it = g_gawkedAt.begin(); it != g_gawkedAt.end();)
            it = now - it->second > std::chrono::minutes(g_LifeGawkCooldownMinutes + 1) ? g_gawkedAt.erase(it)
                                                                                         : std::next(it);
        for (auto it = g_rearAnswered.begin(); it != g_rearAnswered.end();)
            it = now - it->second > std::chrono::minutes(1) ? g_rearAnswered.erase(it) : std::next(it);
    }

    void LetAllGo(const Clock::time_point now)
    {
        for (auto& [guid, v] : g_visits)
        {
            Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
            if (bot && bot->IsInWorld())
                Let(bot, AiOf(bot), v, true, now);
        }
        g_visits.clear();
    }
}

void PbpHangout_Update(uint32_t /*diff*/)
{
    const auto now = Clock::now();
    if (!g_Pbp.enable || !g_HangoutEnable)
    {
        if (!g_visits.empty())
            LetAllGo(now);
        return;
    }

    for (auto it = g_visits.begin(); it != g_visits.end();)
    {
        Visit& v = it->second;
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->first));
        PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
        if (!ai)
        {
            it = g_visits.erase(it);   // logged out: nothing to put back
            continue;
        }
        if (v.phase == Phase::Arriving && now - v.started > std::chrono::seconds(60))
        {
            // Never landed (or not where it was sent): nothing of this to undo.
            ++g_stats.gaveUp;
            ++g_stats.lostLanding;
            g_cooldown[it->first] = now + std::chrono::minutes(g_HangoutCooldownMinutes);
            it = g_visits.erase(it);
            continue;
        }
        if (!bot->IsInWorld() || bot->IsBeingTeleported())
        {
            ++it;   // between maps: looked at again once it lands
            continue;
        }

        PbpHangoutSpot const* spot = SpotByKey(v.spotKey);
        bool keep = spot != nullptr;   // a spot taken out of the conf: everyone there goes
        if (!keep)
            Let(bot, ai, v, true, now);
        else if (v.phase == Phase::Arriving)
            keep = Arrive(bot, ai, v, *spot, now);
        else if (v.phase == Phase::Travel)
            keep = Travel(bot, ai, v, now);
        else if (v.phase == Phase::Approach)
            keep = Approach(bot, ai, v, now);
        else if (v.phase == Phase::Duelling)
            keep = Duel(bot, ai, v, now);
        else if (v.phase == Phase::Flying)
            keep = Fly(bot, ai, v, *spot, now);
        else if (v.phase == Phase::Errand)
            keep = Errand(bot, ai, v, now);
        else
        {
            keep = Hold(bot, ai, v, now);
            if (keep && now >= v.nextLook)
            {
                v.nextLook = now + std::chrono::seconds(g_HangoutCheckSeconds);
                keep = Stay(bot, ai, v, now);
            }
        }
        it = keep ? std::next(it) : g_visits.erase(it);
    }

    static Clock::time_point nextLife{};
    if (now >= nextLife)
    {
        nextLife = now + std::chrono::seconds(1);
        Life(now);
    }

    static Clock::time_point nextRecruit{};
    if (now < nextRecruit)
        return;
    nextRecruit = now + std::chrono::seconds(g_HangoutCheckSeconds);
    Recruit(now);

    for (auto it = g_cooldown.begin(); it != g_cooldown.end();)
        it = (now >= it->second) ? g_cooldown.erase(it) : std::next(it);
    for (auto it = g_spotNextFetch.begin(); it != g_spotNextFetch.end();)
        it = (now >= it->second) ? g_spotNextFetch.erase(it) : std::next(it);
    for (auto it = g_cityNextFetch.begin(); it != g_cityNextFetch.end();)
        it = (now >= it->second) ? g_cityNextFetch.erase(it) : std::next(it);
}

bool PbpHangout_IsAway(Player* bot)
{
    if (!bot || g_visits.empty())
        return false;
    auto it = g_visits.find(bot->GetGUID().GetRawValue());
    if (it == g_visits.end() || !it->second.afkSet)
        return false;
    if (Wanted(bot))
    {
        // Invited, traded with, attacked: back at the keyboard, and done here.
        ++g_stats.cutShort;
        Let(bot, AiOf(bot), it->second, false, Clock::now());
        g_visits.erase(it);
        return false;
    }
    return bot->isAFK();
}

void PbpHangout_ComeBack(Player* bot)
{
    if (!bot || g_visits.empty())
        return;
    auto it = g_visits.find(bot->GetGUID().GetRawValue());
    if (it == g_visits.end() || !it->second.afkSet)
        return;
    if (bot->isAFK())
        bot->ToggleAFK();
    it->second.afkSet   = false;
    it->second.cameBack = Clock::now();
    ++g_stats.cameBack;
}

bool PbpHangout_UsingBench()
{
    return t_usingBench;
}

bool PbpHangout_Get(Player* bot, std::string& spot, std::string& doing, bool& away, bool& justBack)
{
    if (!bot || g_visits.empty())
        return false;
    auto it = g_visits.find(bot->GetGUID().GetRawValue());
    if (it == g_visits.end() || it->second.phase != Phase::Settled)
        return false;
    const Visit& v = it->second;
    spot     = v.spotName;
    doing    = DoingAt(v, bot);
    away     = v.afkSet && bot->isAFK();
    justBack = v.cameBack != Clock::time_point{} && Clock::now() - v.cameBack < std::chrono::minutes(3);
    return true;
}

PbpHangoutStats PbpHangout_GetStats()
{
    PbpHangoutStats s = g_stats;
    s.spots = static_cast<uint32_t>(g_HangoutSpots.size());
    for (const auto& [key, places] : g_fishPlaces)
        s.fishPlaces += static_cast<uint32_t>(places.size());
    for (const auto& [zone, services] : g_services)
        s.services += static_cast<uint32_t>(services.size());
    for (const auto& [key, known] : g_spotInns)
        if (known.known && !known.inns.empty())
            ++s.withInn;
    for (const auto& [guid, v] : g_visits)
    {
        if (v.phase == Phase::Settled)
        {
            ++s.hanging;
            if (v.afkSet)
                ++s.away;
        }
        else if (v.phase == Phase::Duelling)
        {
            ++s.hanging;
            ++s.duelling;
        }
        else
            ++s.onTheWay;
    }
    return s;
}

bool PbpHangout_SendToDuel(Player* bot, const std::string& spotKey)
{
    if (!bot || !g_Pbp.enable || !g_HangoutEnable)
        return false;
    PbpHangoutSpot const* s = SpotByKey(spotKey);
    PlayerbotAI* ai = AiOf(bot);
    // (Playerbots' "go camp", which walks it there, is part of its "new rpg".)
    if (!s || !ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot) ||
        !ai->HasStrategy("new rpg", BOT_STATE_NON_COMBAT) || Wanted(bot) || !bot->IsInWorld() ||
        bot->IsBeingTeleported() || bot->GetMapId() != s->map || bot->IsInFlight() || bot->GetVehicle())
        return false;
    const auto now = Clock::now();
    const uint64_t raw = bot->GetGUID().GetRawValue();
    if (auto it = g_visits.find(raw); it != g_visits.end())
    {
        if (it->second.phase == Phase::Arriving || it->second.phase == Phase::Duelling ||
            it->second.phase == Phase::Flying)
            return false;   // landing, in the air, or in a duel already
        // Done with how it was there: up on its feet and back at the keyboard.
        Let(bot, ai, it->second, false, now);
        g_visits.erase(it);
    }
    g_cooldown.erase(raw);
    if (!Send(bot, ai, *s, now))
        return false;
    g_visits[raw].forDuel = true;
    if (g_PbpDebug)
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} heads out to {} for a duel.", bot->GetName(), s->name);
    return true;
}

void PbpHangout_MakeRoom(const std::string& spotKey, uint32_t count, const std::vector<uint64_t>& keep)
{
    PbpHangoutSpot const* s = SpotByKey(spotKey);
    if (!s || !count)
        return;
    const auto kept = [&keep](uint64_t guid) { return std::find(keep.begin(), keep.end(), guid) != keep.end(); };
    uint32_t there = 0;
    std::vector<uint64_t> settled;
    for (const auto& [guid, v] : g_visits)
    {
        if (v.spotKey != spotKey || kept(guid))
            continue;
        ++there;
        if (v.phase == Phase::Settled && !v.forDuel)
            settled.push_back(guid);
    }
    const uint32_t room = RoomAt(*s);
    if (there + count <= room)
        return;
    uint32_t over = there + count - room;
    std::shuffle(settled.begin(), settled.end(), Rng());
    const auto now = Clock::now();
    for (uint64_t guid : settled)
    {
        if (!over)
            break;
        auto it = g_visits.find(guid);
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
        if (it == g_visits.end() || !bot || !bot->IsInWorld())
            continue;   // gone: the tick tidies it up
        PlayerbotAI* ai = AiOf(bot);
        ++g_stats.movedOn;
        Let(bot, ai, it->second, true, now);
        const bool back = it->second.broughtIn && SendBack(bot, ai, it->second);
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} heads off from {} to make room for a duel{}.",
                     bot->GetName(), it->second.spotName, back ? ", back where it came from" : "");
        g_visits.erase(it);
        --over;
    }
}

bool PbpHangout_Has(Player* bot)
{
    return bot && !g_visits.empty() && g_visits.contains(bot->GetGUID().GetRawValue());
}

bool PbpHangout_SettledAt(Player* bot, const std::string& spotKey)
{
    if (!bot || g_visits.empty())
        return false;
    auto it = g_visits.find(bot->GetGUID().GetRawValue());
    return it != g_visits.end() && it->second.spotKey == spotKey && it->second.phase == Phase::Settled;
}

PbpHangoutMove PbpHangout_MoveTo(Player* bot, float x, float y, float z)
{
    if (!bot || g_visits.empty())
        return PbpHangoutMove::NotHere;
    auto it = g_visits.find(bot->GetGUID().GetRawValue());
    if (it == g_visits.end())
        return PbpHangoutMove::NotHere;
    Visit& v = it->second;
    if (v.phase == Phase::Duelling)
        return PbpHangoutMove::NotHere;
    if (v.phase == Phase::Arriving || v.phase == Phase::Travel || v.phase == Phase::Flying ||
        v.phase == Phase::Errand)
        return PbpHangoutMove::OnItsWay;
    PlayerbotAI* ai = AiOf(bot);
    if (!ai)
        return PbpHangoutMove::NotHere;
    // Moved on from whatever city life had it doing: its place is the new one.
    if (v.act == Act::Dancing && bot->GetUInt32Value(UNIT_NPC_EMOTESTATE) == EMOTE_STATE_DANCE)
        bot->SetUInt32Value(UNIT_NPC_EMOTESTATE, EMOTE_ONESHOT_NONE);
    PutAwayPole(bot, v);
    v.act  = Act::None;
    v.away = false;

    // Its place from now on. Back at the keyboard to move (out of a fire, or
    // out of someone's way), and away again once it is there, if it was (Hold).
    v.x = x;
    v.y = y;
    v.z = z;
    if (v.afkSet && bot->isAFK())
        bot->ToggleAFK();
    v.afkSet  = false;
    v.holding = false;
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    if (v.posture == Posture::Bench)
    {
        v.posture = Posture::Sit;   // the bench is in it: on the ground nearby instead
        v.bench   = ObjectGuid::Empty;
    }
    v.phase         = Phase::Approach;
    v.approachStart = Clock::now();
    ai->SetNextCheckDelay(kHoldMs);
    bot->GetMotionMaster()->MovePoint(kPointId, x, y, z);
    return PbpHangoutMove::Moved;
}

std::vector<PbpHangoutCity> PbpHangout_Cities()
{
    std::vector<PbpHangoutCity> cities;
    std::unordered_map<std::string, size_t> byName;
    std::unordered_map<std::string, size_t> bySpot;
    for (PbpHangoutSpot const& s : g_HangoutSpots)
    {
        const std::string city = PbpCityOf(s.name.empty() ? s.key : s.name);
        auto [at, fresh] = byName.emplace(city, cities.size());
        if (fresh)
        {
            PbpHangoutCity c;
            c.name = city;
            cities.push_back(c);
        }
        PbpHangoutCity& c = cities[at->second];
        c.room += RoomAt(s);
        c.share += s.capacity;
        c.full += g_full.contains(s.key) ? 1 : 0;
        bySpot[s.key] = at->second;
    }
    for (const auto& [guid, v] : g_visits)
    {
        auto at = bySpot.find(v.spotKey);
        if (at == bySpot.end())
            continue;
        if (v.phase == Phase::Settled || v.phase == Phase::Duelling)
        {
            ++cities[at->second].there;
            if (PbpHangoutSpot const* s = SpotByKey(v.spotKey); s && s->hot)
                ++cities[at->second].hot;
        }
        else
            ++cities[at->second].onTheWay;
    }
    return cities;
}

std::vector<std::string> PbpHangout_SpotReport()
{
    std::vector<std::string> out;
    const auto now = Clock::now();
    std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> at;   // spot -> settled, on the way
    for (const auto& [guid, v] : g_visits)
    {
        auto& [settled, onTheWay] = at[v.spotKey];
        ++(v.phase == Phase::Settled || v.phase == Phase::Duelling ? settled : onTheWay);
    }
    for (const PbpHangoutSpot& s : g_HangoutSpots)
    {
        const auto [settled, onTheWay] = at[s.key];
        std::string line = fmt::format("{} ({}{}): {} there, {} on the way, room {}", s.key,
                                       PbpCityOf(s.name.empty() ? s.key : s.name), s.hot ? ", hot" : "", settled,
                                       onTheWay, RoomAt(s));
        if (RoomAt(s) != s.capacity)
            line += fmt::format(" (its share {})", s.capacity);
        if (auto full = g_full.find(s.key); full != g_full.end())
            line += fmt::format(", full with {} for {} min", full->second.held,
                                std::chrono::duration_cast<std::chrono::minutes>(now - full->second.since).count());
        auto it = g_looks.find(s.key);
        if (it == g_looks.end())
        {
            out.push_back(line + "; not looked at yet.");
            continue;
        }
        const Looks& l = it->second;
        const uint64_t looked = l.Looked();
        line += fmt::format("; found full {} times, no room for a bot fetched there {} times. Places looked at {}: {} "
                            "taken up; turned down {} not at its height, {} in water, {} out of sight of its middle, "
                            "{} someone's already, {} under a roof, {} by a fire, {} with someone standing there, {} "
                            "no way there from its middle.",
                            l.full, l.noRoom, looked, l.found, l.ground, l.water, l.sight, l.taken, l.roof, l.fire,
                            l.crowded, l.path);
        out.push_back(line);
    }
    return out;
}

void PbpHangout_At(const std::string& spotKey, std::vector<Player*>& settled, uint32_t& duelling)
{
    settled.clear();
    duelling = 0;
    for (const auto& [guid, v] : g_visits)
    {
        if (v.spotKey != spotKey || (v.phase != Phase::Settled && v.phase != Phase::Duelling))
            continue;
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
        if (!bot || !bot->IsInWorld())
            continue;
        if (v.phase == Phase::Duelling)
        {
            if (bot->duel)
                ++duelling;
        }
        else
            settled.push_back(bot);
    }
}

void PbpHangout_NoteRear(Player* player)
{
    if (!player || !g_LifeEnable || !g_LifeRearBackChance)
        return;
    NoteRear(player->GetGUID());
}

void PbpHangout_Busy(Player* bot, uint32_t seconds)
{
    if (!bot || g_visits.empty())
        return;
    auto it = g_visits.find(bot->GetGUID().GetRawValue());
    if (it == g_visits.end())
        return;
    Visit& v = it->second;
    const auto now = Clock::now();
    if (!seconds)
    {
        // Done talking: back to its own place, if it walked over to talk.
        v.busyUntil = Clock::time_point{};
        if (v.act == Act::Chatting)
            if (PlayerbotAI* ai = AiOf(bot))
                EndAct(bot, ai, v, now);
        return;
    }
    v.busyUntil = std::max(v.busyUntil, now + std::chrono::seconds(seconds));
}

void PbpHangout_Spoke(Player* speaker)
{
    if (!speaker || !speaker->IsInWorld() || !g_Pbp.enable || !g_HangoutEnable || !g_LifeEnable ||
        !g_LifeHeadTurnChance || g_visits.empty())
        return;
    const auto now = Clock::now();
    const uint64_t self = speaker->GetGUID().GetRawValue();
    for (auto& [guid, v] : g_visits)
    {
        if (guid == self || v.map != speaker->GetMapId() || v.phase != Phase::Settled || v.afkSet ||
            v.posture == Posture::Bench || now < v.busyUntil || std::hypot(v.x - speaker->GetPositionX(),
                                                                           v.y - speaker->GetPositionY()) > 12.0f)
            continue;
        if (std::uniform_int_distribution<uint32_t>(1, 100)(Rng()) > g_LifeHeadTurnChance)
            continue;   // (not Roll: out here that is the core's group loot roll)
        v.turnTo = speaker->GetGUID();
        v.turnAt = now + std::chrono::milliseconds(std::uniform_int_distribution<int>(400, 1800)(Rng()));
    }
}

void PbpHangout_Gather(std::vector<Player*> const& bots, uint32_t seconds)
{
    if (bots.size() < 2 || !bots.front() || !g_Pbp.enable || !g_HangoutEnable || !g_LifeEnable || g_visits.empty())
        return;
    const auto now = Clock::now();
    Player* first = bots.front();
    for (Player* bot : bots)
        if (bot)
            PbpHangout_Busy(bot, seconds);
    for (size_t i = 1; i < bots.size(); ++i)
    {
        Player* bot = bots[i];
        auto it = bot ? g_visits.find(bot->GetGUID().GetRawValue()) : g_visits.end();
        PlayerbotAI* ai = bot ? AiOf(bot) : nullptr;
        if (it == g_visits.end() || !ai || !bot->IsInWorld() || bot->GetMapId() != first->GetMapId())
            continue;
        Visit& v = it->second;
        const float d = bot->GetExactDist(first);
        if (v.phase != Phase::Settled || v.act != Act::None || v.afkSet || d < 4.0f || d > 25.0f ||
            (v.posture != Posture::Stand && v.posture != Posture::Mounted) || bot->isMoving())
            continue;
        if (GoOverTo(bot, ai, v, first, first->GetAngle(bot) + Frand(-0.5f, 0.5f), Frand(2.0f, 2.8f), Act::Chatting,
                     now + std::chrono::seconds(seconds), now))
            ++g_stats.gathered;
    }
}
