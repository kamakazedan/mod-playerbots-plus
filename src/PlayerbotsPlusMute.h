#ifndef PLAYERBOTS_PLUS_MUTE_H
#define PLAYERBOTS_PLUS_MUTE_H

#include "ScriptMgr.h"

#include <cstdint>
#include <string>
#include <vector>

class WorldPacket;
class WorldSession;

// --------------------------------------------------------------------------
// Stock lines playerbots whispers that nobody needs to see: "Using
// [Hearthstone]", "AI was reset to defaults", "I don't know this dungeon,
// lead the way!". PlayerbotsPlus.Mute.Patterns lists them; with a chat
// module that says the rest in its own words ("Staying", "Following",
// "Hello!"), the conf lists those too. Only whispers and party or raid lines
// from a bot are looked at, and lines a module sends for a bot while it holds
// a PlayerbotsPlus::OwnLine always go through.
//
// The packet hook runs on whatever thread sends the packet, so it reads
// nothing of the world: only the published pattern list and the set of bot
// guids, both refreshed on the world thread.
// --------------------------------------------------------------------------

// Publish PlayerbotsPlus.Mute.* for the packet hook. World thread, at config
// load.
void PbpMute_Publish(bool enable, std::vector<std::string> patterns);

// World tick: refresh which characters online are bots.
void PbpMute_Update(uint32_t diff);

// Lines dropped so far.
uint64_t PbpMute_GetDropped();

// PlayerbotsPlus::OwnLine: lines sent on this thread between these are never
// muted.
void PbpMute_OwnLineBegin();
void PbpMute_OwnLineEnd();

class PbpMuteScript : public ServerScript
{
public:
    PbpMuteScript() : ServerScript("PbpMuteScript", { SERVERHOOK_CAN_PACKET_SEND }) {}
    bool CanPacketSend(WorldSession* session, WorldPacket const& packet) override;
};

#endif // PLAYERBOTS_PLUS_MUTE_H
