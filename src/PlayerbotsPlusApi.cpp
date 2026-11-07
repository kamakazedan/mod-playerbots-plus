#include "PlayerbotsPlus.h"
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
