#include "PlayerbotsPlus.h"
#include "PlayerbotsPlusDrops.h"
#include "PlayerbotsPlusDuels.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusMute.h"
#include "PlayerbotsPlusUtil.h"

#include "Player.h"

#include <utility>
#include <vector>

// PlayerbotsPlus.h, apart from the market's listeners (PlayerbotsPlusMarket).

namespace
{
    std::vector<PlayerbotsPlus::BotEventListener> g_botListeners;
}

void PbpRaiseBotEvent(PlayerbotsPlus::BotEvent const& event)
{
    // By index: a listener that adds another does not pull the list from
    // under this.
    for (size_t i = 0; i < g_botListeners.size(); ++i)
        if (g_botListeners[i])
            g_botListeners[i](event);
}

namespace PlayerbotsPlus
{
    bool GetHangout(Player* bot, HangoutState& out)
    {
        HangoutState state;
        if (!PbpHangout_Get(bot, state.spot, state.doing, state.away, state.justBack))
            return false;
        out = std::move(state);
        return true;
    }

    bool IsAway(Player* bot)
    {
        return PbpHangout_IsAway(bot);
    }

    void ComeBack(Player* bot)
    {
        PbpHangout_ComeBack(bot);
    }

    bool UsingBench()
    {
        return PbpHangout_UsingBench();
    }

    bool CanGoDuel(Player* a, Player* b, std::string& where)
    {
        return PbpDuels_CanGo(a, b, where);
    }

    bool GoDuel(Player* a, Player* b)
    {
        return PbpDuels_Go(a, b);
    }

    double DropChance(uint32_t itemId)
    {
        return PbpDrops_ChancePct(itemId);
    }

    bool DropSourcesOf(uint32_t itemId, DropSources& out)
    {
        out = {};
        PbpDropsFrom from;
        if (!PbpDrops_Sources(itemId, from))
            return false;
        out.kinds    = from.kinds;
        out.spawns   = from.spawns;
        out.minLevel = from.minLevel;
        out.maxLevel = from.maxLevel;
        for (size_t i = 0; i < 3; ++i)
        {
            out.creature[i]  = from.creature[i];
            out.chancePct[i] = from.chancePct[i];
        }
        out.bestPct = from.bestPct;
        out.onlyMap = from.onlyMap;
        out.chests  = from.chests;
        return true;
    }

    void CityBusy(Player* bot, uint32_t seconds)
    {
        PbpHangout_Busy(bot, seconds);
    }

    void CitySpoke(Player* speaker)
    {
        PbpHangout_Spoke(speaker);
    }

    void CityGather(std::vector<Player*> const& bots, uint32_t seconds)
    {
        PbpHangout_Gather(bots, seconds);
    }

    void SetTemperamentProvider(TemperamentProvider provider)
    {
        PbpHangout_SetTemperamentProvider(std::move(provider));
    }

    void CityCalmDown(Player* bot)
    {
        PbpHangout_CalmDown(bot);
    }

    void AddBotEventListener(BotEventListener listener)
    {
        if (listener)
            g_botListeners.push_back(std::move(listener));
    }

    OwnLine::OwnLine()
    {
        PbpMute_OwnLineBegin();
    }

    OwnLine::~OwnLine()
    {
        PbpMute_OwnLineEnd();
    }
}
