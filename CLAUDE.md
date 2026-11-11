# CLAUDE.md — mod-playerbots-plus

Guidance for working in this module. AzerothCore's and mod-playerbots' own
rules still apply where they touch this code.

## What belongs here

Things random bots *do* that need no language model: trading, moving, getting
unstuck, getting back up. Anything a bot *says*, and anything read out of what
a player types, belongs in mod-ollama-chat. When a feature here wants a bot to
speak, it raises an event through `PlayerbotsPlus.h` and lets the chat module
voice it. This module must build and run without mod-ollama-chat.

## World thread only

Everything here runs on the world thread: `WorldScript::OnUpdate`, the auction
house hooks, and commands. Map threads are idle there, which is the only reason
it is safe to touch a bot's `Player`, its inventory, its mail and its
playerbots AI. Do not add threads. Do not call into this module from a map
thread or a packet hook.

These hooks are the exception, and they touch nothing of the world:

- `PbpStuckScript::OnAuraApply` runs on map threads. It only notes the mount
  under a mutex; the world tick looks at the notes.
- `PbpFireScript::OnDamage` runs on map threads. It only notes who was burned
  by damage nobody dealt (a trap of the world's own) under a mutex; the world
  tick finds the fire and walks the bot out of it.
- `PbpMuteScript::CanPacketSend` runs on whatever thread sends the packet. It
  reads only a snapshot (patterns and bot guids) published from the world
  thread, and `PlayerbotsPlus::OwnLine` is a thread-local counter.
- `PbpLedgerPlayerScript`'s hooks (money, loot, quest rewards, crafting,
  vendor buys, gear put on, consumables used) run on map threads, and the
  ledger's auction notes may come from any thread. They read the session's
  own account id and the item's template, and note under a mutex; the world
  tick decides whose bot it was (`IsInRandomAccountList`, the character
  cache) and adds it up. Never look up a random bot from a map thread.

## An Item* in a hook may already be freed

Hooks run in module order (alphabetical), and a module before this one may
destroy the item its hook is about. mod-junk-to-gold sells and destroys a
bot's grey loot inside its own `OnPlayerLootItem`, and a new item destroyed
is deleted there and then (`Item::SetState(ITEM_REMOVED)` on an `ITEM_NEW`
item is `delete this`). Following the `Item*` that `OnPlayerLootItem` then
hands this module read freed memory, and took the server down a couple of
minutes after the bots came online.

So the loot, group-roll, quest-reward, crafting and vendor hooks never follow
their `Item*`. What they need is read in `OnPlayerStoreNewItem`, which runs
inside `StoreNewItem`, before any of those hooks, while the item is there, and
matched by its address on the same thread (`PlayerbotsPlusLedger`'s `Stored`).
An item being put on (`OnPlayerEquip`) or used to cast (`OnPlayerSpellCast`)
is there to read.

## Talking is the chat module's

Nothing here makes a bot say anything. Where a bot would say something, raise a
`PlayerbotsPlus::BotEvent` (`PbpRaiseBotEvent`) or a market event, with the
facts a prompt needs (names, not pointers), and let a listener voice it. Add a
new event type to `PlayerbotsPlus.h` and bump `PLAYERBOTS_PLUS_API_VERSION`
when a listener needs to know it is there.

## Auctions and mail copy the game's own handlers

`CreateAuction`, `BuyOut` and `CollectMail` in `PlayerbotsPlusMarket.cpp` do
what `HandleAuctionSellItem`, `HandleAuctionPlaceBid` (buyout branch),
`HandleMailTakeMoney` and `HandleMailTakeItem` do. The difference is that they
skip the auctioneer and mailbox distance checks. Each one keeps the same order,
the same checks, and one transaction that saves the items, money and mail
together, so a crash never duplicates or loses anything.

When the core changes those handlers, compare them again. Mail is persisted
with the core's own prepared statements (`CHAR_DEL_MAIL_BY_ID`,
`CHAR_UPD_MAIL`, ...), because `Player::_SaveMail` is not public. A mail
removed with `RemoveMail` is deleted here: the core leaves that to its caller.

`Rename` in `PlayerbotsPlusPets.cpp` does what `HandlePetRename` does, in the
same order, without the declined names a client sends. It only touches a pet
that still has the name it was tamed with (its family's: "Cat"), because
playerbots' own "rename pet" command leaves the game's renamed flag set.

## Playerbots' judgement, not ours

Whether a bot can spare something or wants it comes from playerbots' own
`ItemUsageValue`: `ITEM_USAGE_AH` to sell, `EQUIP` or `REPLACE` to buy gear,
`SKILL` to buy materials. It is computed on a local `ItemUsageValue`, not with
`GetValue<ItemUsage>("item usage", q)`. A context value is kept for every
qualifier for as long as the bot is logged in, and the market asks about
thousands of different auctions.

## Pure files are tested

`PlayerbotsPlusMarketPrice.{h,cpp}` and `PlayerbotsPlusText.{h,cpp}` have no
game types in them, so they can be tested without a server. Keep it that way:
`tests/build.sh`. The text test also checks the conf against the code: every
key the code reads is in the conf, and the default mute list, hangout spots,
companion lists and pet names are the same in both.

## Conventions

- Header names start with `PlayerbotsPlus`. Every module's `src/` is on one
  shared include path, so a `Config.h` or `Market.h` here would shadow
  someone else's.
- Log to `module.playerbotsplus`. Startup and registration lines go to
  `server.loading`.
- Every setting goes in `conf/mod_playerbots_plus.conf.dist` with a comment
  saying what it does and its default. That file is the documentation.
- `urand(min, max)` is inclusive at both ends. Use the module's own `Between`
  and `Roll`.
- AzerothCore compiles only `src/`. Tests and tools go outside it.
- New tables go in `data/sql/db-characters/base/` (the core applies a module's
  `data/sql/*characters*` to the characters database) as `CREATE TABLE IF NOT
  EXISTS`. The one there is the economy ledger's, `playerbots_plus_ledger`;
  `PbpLedger_Load` makes it too, for a server whose updater is off. Market
  state is in memory and rebuilt from the auction house itself.
- A bot's gold changes outside `ModifyMoney` too: playerbots' refresh and
  level resets use `SetMoney`, which no hook sees. The ledger finds those by
  setting what a bot has against what the hooks saw, at each look.

## Checking without a full build

`g++ -std=gnu++20 -fsyntax-only -include Log.h` against the core's and
mod-playerbots' include directories catches signature and declaration
mistakes. It does not link.
