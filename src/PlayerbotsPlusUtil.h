#ifndef PLAYERBOTS_PLUS_UTIL_H
#define PLAYERBOTS_PLUS_UTIL_H

#include "PlayerbotsPlus.h"

#include <string>

class Player;
class PlayerbotAI;

// A playerbots bot: a headless session, or one with a bot AI.
bool PbpIsBotPlayer(Player* player);

inline bool PbpIsRealPlayer(Player* player)
{
    return player && !PbpIsBotPlayer(player);
}

// Someone in this player's group is a real player.
bool PbpGroupHasRealPlayer(Player* who);

PlayerbotAI* PbpAiOf(Player* player);

// The zone it is in, by name ("Elwynn Forest"); empty when unknown.
std::string PbpZoneName(Player* bot);

// To every PlayerbotsPlus::AddBotEventListener listener, now.
void PbpRaiseBotEvent(PlayerbotsPlus::BotEvent const& event);

#endif // PLAYERBOTS_PLUS_UTIL_H
