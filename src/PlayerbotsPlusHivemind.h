#ifndef PLAYERBOTS_PLUS_HIVEMIND_H
#define PLAYERBOTS_PLUS_HIVEMIND_H

// --------------------------------------------------------------------------
// The hivemind (.pbp hivemindsay, hivemindyell, hivemindgeneral,
// hivemindemote; PlayerbotsPlus.Hivemind.*): every bot on the server says,
// yells or puts in its zone's General the text a GM gives it, word for word,
// or does the emote. It is the GM's, not the bots' own talk: no chat module
// voices it or answers it.
//
// The bots go a batch every world tick (Hivemind.BotsPerTick), those nearest
// a real player first: three thousand bots saying something in one tick
// would hold that tick up for everyone. A dance, or sitting, kneeling or
// lying down, lasts until the bot moves or Hivemind.StateSeconds pass -- a
// bot has no game client to stop it, and would dance on as it walked off.
//
// World thread only (the commands, and the world tick).
// --------------------------------------------------------------------------

#include "ObjectGuid.h"

#include <cstdint>
#include <string>
#include <vector>

enum class PbpHivemindKind : uint8_t
{
    Say,
    Yell,
    General,   // its zone's General channel
    Emote
};

// The text emote with that name, as the game's slash commands go ("laugh",
// "/laugh", "lol", "LAUGH"): its TEXT_EMOTE_* id, 0 for none.
uint32_t PbpHivemind_EmoteId(std::string name);

// A few emote names, for a GM who asked for one there is not.
std::string PbpHivemind_SomeEmotes();

// Every bot online, from the next world tick: `text` (Say, Yell, General) or
// `emote` (a TEXT_EMOTE_* id). `by`, whoever asked (empty: the console), is
// told how it went once every bot has had its turn. The number of bots it
// goes to; 0, and `why` filled in, when it does not go at all.
uint32_t PbpHivemind_Start(PbpHivemindKind kind, std::string const& text, uint32_t emote, ObjectGuid by,
                           std::string& why);

// World tick. With the module or the hivemind switched off, what is waiting
// is dropped -- but a dance or a sit the hivemind started is still ended.
void PbpHivemind_Update(uint32_t diff);

struct PbpHivemindStats
{
    uint64_t commands = 0;   // hivemind commands given
    uint64_t lines    = 0;   // ...said, yelled, put in General or emoted, by one bot each
    uint64_t missed   = 0;   // ...bots that could not: gone, dead, in no General, between maps
    uint32_t waiting  = 0;   // bots still to have their turn
};
PbpHivemindStats PbpHivemind_GetStats();

#endif // PLAYERBOTS_PLUS_HIVEMIND_H
