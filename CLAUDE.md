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

## Playerbots' judgement, not ours

Whether a bot can spare something or wants it comes from playerbots' own
`ItemUsageValue`: `ITEM_USAGE_AH` to sell, `EQUIP` or `REPLACE` to buy gear,
`SKILL` to buy materials. It is computed on a local `ItemUsageValue`, not with
`GetValue<ItemUsage>("item usage", q)`. A context value is kept for every
qualifier for as long as the bot is logged in, and the market asks about
thousands of different auctions.

## Pricing is pure

`PlayerbotsPlusMarketPrice.{h,cpp}` has no game types in it, so it can be
tested without a server. Keep it that way: `tests/build.sh`.

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
- New tables go in `data/sql/<db>/base/` as `CREATE TABLE IF NOT EXISTS`.
  There are none yet: market state is in memory and rebuilt from the auction
  house itself.

## Checking without a full build

`g++ -std=gnu++20 -fsyntax-only -include Log.h` against the core's and
mod-playerbots' include directories catches signature and declaration
mistakes. It does not link.
