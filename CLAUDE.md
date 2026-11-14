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
- `PbpRearScript::CanPacketReceive` sees every packet anyone sends, on
  whatever thread handles it. It looks at the opcode only, and for a mount
  reared (`CMSG_MOUNTSPECIAL_ANIM`, which the world thread handles) notes who
  under a mutex; city life's tick has those near do it back.
- `PbpMuteScript::CanPacketSend` runs on whatever thread sends the packet. It
  reads only a snapshot (patterns and bot guids) published from the world
  thread, and `PlayerbotsPlus::OwnLine` is a thread-local counter.
- `PbpDungeonsPacketScript::OnPacketSent` runs on whatever thread sends the
  packet. It only notes a dungeon finder offer (who, which) under a mutex;
  the world tick answers it.
- `PbpDungeonsTeleportScript::OnPlayerBeforeTeleport` runs on whatever
  thread teleports the player. It reads only the list of run members and
  their dungeon (`g_keepIn`) that the world tick publishes under a mutex, and
  turns down a living member's teleport off that map.
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

## City hangouts are shared out by playerbots' weights

With `Hangout.CityWeights` on, a spot's written capacity is only its part
against the other spots of its kind (hot or not) in its city: the places
(`Hangout.Places`) are shared among the cities by playerbots' own
`Playerbots.TeleTo<City>Weight` (read from the conf with playerbots' keys
and defaults, so it does not matter that playerbots loads its config after
this), and `Hangout.HotShare` of a city's places go to its hot spots. The
city is the end of the spot's name (`PbpCityOf`); the sharing is pure and
tested (`PbpShareHangoutPlaces`). A spot whose capacity outgrows its radius
grows (`Hangout.RoomPerBot`): crowded, `PickPlace` finds nowhere to stand.

A spot where `PickPlace` finds nowhere is full (`FoundFull`): it keeps the
room it holds, and `ShareRoom` gives the rest out (`PbpShareFullRoom`, pure
and tested) -- its city's spots of its kind, its city's others, then every
open spot -- until fewer are there or `Hangout.FullMinutes` pass. Use
`RoomAt(spot)`, never `spot.capacity`, for how many a spot takes now.
`PickPlace` counts why it turns places down per spot (`.pbp hangouts`).

## City life lives in the hangouts

What the crowd does while it hangs out (`PlayerbotsPlus.CityLife.*`) is part
of a visit in `PlayerbotsPlusHangout.cpp`, not a module of its own: a step, a
hop, an errand, fishing, a look at a rare mount, a dance or walking over to
talk is the visit's `Act` (or its `Errand` and `Flying` phases), started only
when it is `FreeForLife` and near a real player, and undone by `EndAct` and
`Let` -- the dance state off, its own weapons back in its hands. Moving it
goes through `Shift` (its AI stays held); a walk across the city (an errand,
a flight) frees the AI and uses playerbots' own `go camp` and
`ChangeToTravelFlight`. The sums with no game in them are
`PlayerbotsPlusCityLifeText` (tested).

The chat module tells it what it cannot see (`CityBusy`, `CitySpoke`,
`CityGather`, API 5), and voices `BotEventType::CityMountShown` and
`CityDance`. Those calls can come from inside a raised event (a dance emote
played while the hangouts raise `CityDance`), so they only ever change a
visit's fields, never add or remove one: a loop over `g_visits` may be under
way.

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

## Bot dungeons and mod-dungeon-clear

Bot dungeons (`PlayerbotsPlusDungeons`, `PlayerbotsPlusDungeonQuests`) are
built only with mod-dungeon-clear in the build: `PBP_BOT_DUNGEONS` is 1 when
its `DcBotRun.h` is there (`DC_BOT_RUN_API`), and both files are stubs
otherwise. This module must still build and run without it; check both ways.

- The clearing is mod-dungeon-clear's, through `DcBotRun`: `Start` a group's
  run, read how it stands with `Get`, `Stop` it. What a run is, how it is
  judged (its watchdogs) and how it ends are decided there. Here: who goes,
  the queue, getting everyone in, and letting them go after.
- Bots queue with `sLFGMgr->JoinLfg` from the world tick, not playerbots'
  packet: JoinLfg is only unsafe from a map thread.
- Which dungeon a free place goes to is decided band by band
  (`BotDungeons.LevelBands`): a dungeon's band is its dungeon finder
  `TargetLevel`, and the order bands are offered a place is
  `PbpDungeons::BandOrder` (pure, tested), from the runs and queued parties
  each band has (`RunsByBand`) against its share. Keep the choice there.
- A new party waits while the world tick is slow (`MaxWorldTickMs`) or the
  machine is short of memory (`MinFreeMemoryMB`): /proc/meminfo's
  MemAvailable, parsed in the pure file and read by
  `PbpDungeons_FreeMemoryMb`, -1 where there is none to read. Unknown is
  never short: it holds nothing back, and startup says it is not checked.
- Playerbots vetoes every dungeon finder offer with no player, self-bot or
  party in it (`PlayerbotsGlobalScript::CanCreateLfgProposal`), so five bots
  queued each on their own only ever go with a player. After `AloneMinutes`
  they leave the queue, are made a party (`Group::Create`, the tank leading)
  and queue as one, the role check answered here (`UpdateRoleCheck`). The
  dungeon finder turns that party itself into the dungeon group
  (`ConvertToLFG`). Playerbots' "leave a group a random bot leads" is in
  `ProcessBot`, which skips grouped bots.
- Playerbots says no to a dungeon finder offer for a bot that is fighting or
  dead, and one no ends the offer for all five. So the offer is said yes to
  (`UpdateProposal`) for the bots this queued in the world tick it goes out,
  before their AI reads it: `OnPacketSent` fires for a bot's session
  (`CanPacketSend` does not). Each offer once per bot, since every yes sends
  it to everyone again; and the playerbots `Refresh` that goes with a yes
  only before the group exists, because it leaves any group.
- A group with nobody playing in it has no master for its bots, so the
  wander-about strategies a random bot gets on its own (grind, rpg) stay on
  it, and mod-dungeon-clear only holds them back on the tank. They come off
  for the run (and again when playerbots resets them), and `ResetStrategies`
  gives them back once the bot is out of the group.
- A member of a run that dies is kept down (`KeepTheDeadDown`, every tick):
  playerbots releases a dead bot with no player in its group at its next AI
  tick, the group's leader always (mod-dungeon-clear's PreventBotRelease
  stops that in a dungeon; this makes sure). Its AI is held
  (`SetNextCheckDelay`) while it lies dead in the dungeon, and a rez offered
  to it is taken (`ResurectUsingRequestData`, what Accept does).
  mod-dungeon-clear never ticks on a corpse, so holding it costs the run
  nothing; its rez recovery runs on the living.
- A living member is never teleported off its run's dungeon map
  (`PbpDungeonsTeleportScript`): playerbots' level-up teleport
  (`AutoTeleportForLevel` -> `RandomTeleport`) moves a grouped bot only if it
  leads the group, so it took bot dungeons' tanks out mid-run. `Release`
  takes the members off the list (`LetOut`) before it sends them out.
- Walks for quests use playerbots' own "go camp" (`rpgInfo.ChangeToGoCamp`),
  as hangouts do: hangouts leave a bot on its way to a camp alone, and these
  leave a hanging-out one alone (`PbpHangout_Has`).
- With nobody free to heal, one who does the damage heals instead
  (`HealerStepIn`, picked in the pure `PickParty`). From the core's
  `MinDualSpecLevel` it heals on its second spec: dual spec learned as a
  trainer teaches it (spells 63680 and 63624, what the core's gossip casts),
  the spec made playerbots' premade healing one (`InitTalentsBySpecNo`, then
  `InitGlyphs`), `ResetStrategies`. Below that, playerbots' healing combat
  strategy for the class, put back every look while the run goes on
  (playerbots resets strategies for its own reasons). Playerbots never puts a
  random bot on its second spec itself (only a player's `talents switch 2`),
  so a random bot with no master and no group found on it is put back on its
  first (`OwnSpec`, from `Release` and the sweep): that covers a run that
  ended some other way, and a restart.
- Playerbots empties a quest log with fewer than two slots free, dropping
  first what one cannot do alone: every dungeon quest. Keeping
  `KeepLogFree` slots free (dropping what a player would first) is what keeps
  them; the world tick does it between the bot's own ticks.

## Group quests

`PlayerbotsPlusGroupQuests` gets random bots together for the quests
playerbots' questing refuses (`IsQuestCapableDoing`: a Group type, or two or
more suggested players). Built without mod-dungeon-clear too.

- A party asking for help is only a list: everyone on it stays out of a
  group, about its own questing, until it meets. In a group, playerbots has
  members follow the leader, and bots are to keep questing as normal until
  the party is ready.
- Meeting: the leader rests (`ChangeToRest`), the rest walk over (`go camp`)
  and are added to a group as they get there (`Group::Create` with the
  leader, then `AddMember`), stripped of the wander strategies so they follow
  it. Playerbots finds no master in a group of bots (`FindNewMaster`), so
  nothing resets them; `ResetStrategies` gives them back after.
- At it: the leader goes for what the quest is for -- what it asks to be
  killed, or what drops what it asks for (`GetCreatureQuestItemList`, read
  once with the spawns: `targets`, `lairs`): walked up to in sight, pulled
  (`AttackStart` on it) from 30 yards, its spawn camped when it is not about.
  Not left to playerbots' targeting: it skips an elite while `can fight
  elite` is false (anyone in the group with full bags to sell: `should
  sell,can sell`) and any mob five levels up, and the first 26 parties left
  to its questing finished none. Only with nothing known of where they stand
  is the leader's `rpgInfo` set to do the quest (`ChangeToDoQuest`).
  Playerbots' questing looks for a quest's place only within 1500 yards, so
  from further the leader walks there first. The rest follow and fight what
  attacks the party. The kill credit and quest drops are the game's.
- One leaving a group of two breaks the group up (`Group::RemoveMember`
  disbands it): look the group up afresh after every removal.
- A party member being taken away by playerbots' level-up teleport (the
  leader: `AutoTeleportForLevel` moves a grouped bot that leads) is turned
  down (`PbpGroupQuestsTeleportScript`, on a list the world tick publishes).
- Both quest log keepers keep both kinds of quest (`PbpGroupQuests_IsOne`,
  `PbpDungeonQuests_IsOne`): one dropping the other's to make room would
  undo it. Bot dungeons, dungeon-quest walks and hangouts leave a bot in a
  group-quest party alone (`PbpGroupQuests_Busy`), and it leaves theirs.
- What they say is mod-ollama-chat's: `BotEventType::GroupQuest*`.

## Pure files are tested

`PlayerbotsPlusMarketPrice.{h,cpp}`, `PlayerbotsPlusText.{h,cpp}`,
`PlayerbotsPlusLedgerText.{h,cpp}`, `PlayerbotsPlusDungeonsText.{h,cpp}` and
`PlayerbotsPlusGroupQuestsText.{h,cpp}` have no game types in them, so they
can be tested without a server. Keep it that way: `tests/build.sh`. The text
test also checks the conf against the code: every key the code reads is in the
conf, and the default mute list, hangout spots, companion lists and pet names
are the same in both.

## What it tells you: a few lines in game, the rest in a file

`.pbp status` and the parts' own commands (`hangouts`, `dungeons`,
`groupquests`, `economy`) say a few short lines each -- a part's `PbpBrief`:
how it stands, then what looks wrong (`issues`) -- and the full report
(`PlayerbotsPlusReport.cpp`, `Report.File` in the logs folder, every
`Report.Minutes` and on `.pbp report`) has every counter, spot, run and
party. A new part, or a new count, goes in the report; in game only if it
says whether the part works. An issue is only what someone should act on,
with a threshold that keeps it quiet when all is well: never a count that
just grows. The report's economy reads the ledger's table, so it is read at
most once an hour on the clock.

## Conventions

- Header names start with `PlayerbotsPlus`. Every module's `src/` is on one
  shared include path, so a `Config.h` or `Market.h` here would shadow
  someone else's.
- Log to `module.playerbotsplus`. Startup and registration lines go to
  `server.loading`. Its own file (`PlayerbotsPlus.log`) is set up in
  worldserver.conf, not the module conf: the logs are made before module
  confs are read.
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
