# mod-playerbots-plus

Random bots doing what real players do, without an LLM. An AzerothCore module
for servers running [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

Everything here works on its own. With
[mod-ollama-chat](https://github.com/kamakazedan/mod-ollama-chat) (branch
`dangaming`) in the same build, the bots also talk about what they do, in
their own words.

| Feature | What the bots do |
|---|---|
| [The auction house](#the-auction-house) | Sell what they have no use for and buy upgrades, under their own names |
| [City hangouts](#city-hangouts) | Gather where players do in the capitals: on a bench, mounted, or AFK |
| [Stuck on a mount](#stuck-on-a-mount) | Stop getting on and off their mount at a herb they can't pick, and move on |
| [Getting back up](#getting-back-up) | Release and run back when nobody in the group can resurrect them |
| [Raid marks](#raid-marks) | Crowd-control their own mark and sap the star |
| [Muted bot lines](#muted-bot-lines) | Playerbots' stock whispers, like "Using [Hearthstone]", are dropped |

Each one can be switched off on its own. Every setting is documented in
`conf/mod_playerbots_plus.conf.dist`.

## The auction house

Random bots in a capital city go to the auction house now and then, the way
players do:

- **They collect their auction mail:** money from sales, items that came back
  unsold, and things they bought.
- **They list what they have no use for:** cloth, leather, herbs and ore they
  have no profession for, and gear they will never wear. That's whatever
  playerbots itself would take to the auction house rather than a vendor, as
  long as the bot looted or made it.
- **They buy:** an upgrade they can afford (playerbots decides what counts as
  an upgrade), or materials their profession uses to skill up. They put the
  upgrade on straight away.

The auctions are real ones. Each one is in the bot's faction auction house,
under the bot's own name. Real players can buy them, and the bots buy real
players' auctions. The money is real too: the bot pays the deposit, the house
takes its cut, and the sale is paid by mail an hour later, the same as for
anyone.

How a bot prices one item:

1. If someone else already has one up, it asks a little under the cheapest
   one (1 to 5%). That includes the auction house bot's listings.
2. If nobody does, it asks what the item is worth: a multiple of the vendor
   price, higher for better quality.
3. It never asks more than someone of the item's level could sensibly pay.
   Wealth by level is about 2g at 10, 17g at 30, 300g at 60 and 2000g at 80.
4. It never asks so little that a vendor would pay more. If the market is
   already below that, the bot keeps the item for a vendor.
5. If an item comes back unsold, the bot asks 15% less next time. After a
   couple of tries it gives up on that item.

Bots never list what playerbots gives them for free. Playerbots hands every
random bot ammo, food and drink, potions, class reagents, sharpening stones,
weightstones, oils and poisons, and tops them up every few hours. Selling those
would make gold out of nothing, so they stay out of the auction house. So does
anything a vendor sells. A bot that listed such things before this rule takes
those auctions down on its next visit.

Bots don't walk to the auctioneer: being anywhere in the city is enough.
Shattrath and Dalaran count as cities too. Neither has an auction house, so a
bot there uses its faction's, as a player would after flying over.

Settings: `PlayerbotsPlus.Market.*`.

## City hangouts

On a real server everyone parks in the same places: the Trade District, the
Commons, the Valley of Strength, outside the bank in Dalaran. Playerbots never
sends a bot anywhere in particular, so idle bots in a city just wander from
NPC to NPC. With hangouts, they gather where players do.

- **Where:** 14 spots in the capitals, Shattrath and Dalaran come set up. You
  can add your own, or change how many bots each one holds.
- **Who:** idle random bots near a spot, of its zone and floor. A bot that is
  grouped, queued, questing, gathering, fighting or flying is left alone, and
  so is one a real player has targeted.
- **How they spend it:** on a free bench, mounted on their best mount, sat on
  the ground or just standing about, for 5 to 20 minutes. Now and then one
  goes AFK, and it shows `<AFK>`.
- **Keeping spots busy:** playerbots sends few bots to the capitals, so a
  spot short of bots gets some fetched from elsewhere. While someone is in
  the city, a fetched bot arrives at the nearest friendly inn, the way people
  hearth in, and walks over. While nobody is, it's put down at the spot, and
  it goes back to where it came from when it's done, if nobody would see.
- **Letting go:** an invite, a trade, a duel, a queue or a fight lets a bot go
  at once. So does switching hangouts off.

Nothing happens while no real player is online. Every place a bot stands is
checked to be on the spot's own floor, in sight of its middle and reachable on
foot, so never under the city or on a roof.

Settings: `PlayerbotsPlus.Hangout.*`.

## Stuck on a mount

Playerbots' loot code gets a bot off its mount before it opens something. If
the opening fails, for example because another bot has hold of the vein, the
bot waits a moment, and in that pause playerbots puts it back on its mount.
The next try starts with another dismount, so the bot gets on and off on the
spot, every few seconds, for as long as nothing gives.

This module notices: a bot that mounts 4 times in 30 seconds without moving,
and without fighting in between, takes the hint.

- What it was trying to loot is put out of its reach for 10 minutes, so it
  goes and does something else.
- A random bot on its own also stays on foot for 10 minutes, so the loop
  doesn't start again at the next herb. On foot, it walks up to the herb and
  picks it, the way a player would.
- Each one is logged at INFO, with where it happened, who the bot was with,
  and anyone else at the same thing.

Settings: `PlayerbotsPlus.Stuck.*`.

## Getting back up

Playerbots leaves a bot that dies near a player lying where it fell until it's
told what to do. People don't wait to be told. With this module, a dead bot in
a group with a real player gets itself back up:

- **Someone alive can resurrect it:** it waits for the rez, for up to a minute
  of nobody fighting.
- **Nobody can:** it releases after 5 seconds.
- **Its body is in a dungeon or raid:** the ghost runs from the graveyard to
  the entrance and walks in, which brings it back to life there.
- **In the open world:** the ghost walks back to its body, as playerbots
  already does.

Only priests, paladins, shamans and druids who have learned the spell count as
someone who can resurrect. It never acts in battlegrounds or arenas. While a
[mod-dungeon-clear](https://github.com/kamakazedan/mod-dungeon-clear) run is
on, the run decides how its party comes back, and this stays out of it.

Settings: `PlayerbotsPlus.Revive.*`.

## Raid marks

Groups use marks so a plan needs no explaining:

| Mark | Meaning |
|---|---|
| Skull | Kill first. Putting it on a mob starts the fight |
| Cross | Kill next |
| Moon | Polymorph (mage) |
| Star | Sap (rogue) |
| Diamond | Banish or fear (warlock) |
| Circle | Shackle undead (priest) |
| Triangle | Hibernate, roots or cyclone (druid) |
| Square | Freezing trap (hunter) |

- **Crowd control:** each bot that can crowd-control is told which mark is
  its own, and gets playerbots' crowd-control strategy if it lacked it. Mark
  the pack, then put the skull on: as the pack comes in, each bot controls its
  own mark.
- **Sapping the star:** a sap has to land before the pull. Put the star on a
  mob that isn't fighting and a rogue in the group goes into stealth, sneaks
  round behind it and saps it. A rogue that can't, because it has no Sap yet,
  Sap doesn't work on that mob, or it couldn't get into stealth in time,
  leaves the mob alone.
- **The marks are yours:** nothing here puts a mark on anything. If you want
  it, two settings make the skull move by itself when its mob dies: to the
  cross, or else to the nearest mob still fighting the group that nobody
  marked to control. Both are off by default.

Groups that don't mark see none of this. Which class controls which mark can
be changed.

Settings: `PlayerbotsPlus.Marks.*`.

## Muted bot lines

Playerbots whispers a stock line for a lot of what bots do. By default these
never reach you:

- "Using [Hearthstone]", "Drinking [Conjured Water]", "Eating [...]" and
  "Feasting [...]"
- "Equipping [Vigorous Handguards]" and "Opened item: Satchel of Helpful Goods"
- "AI was reset to defaults"
- "I don't know this dungeon, lead the way!"

Only whispers and party or raid lines from bots are looked at, and only whole
lines. Playerbots' red error lines to you, such as "Mungehdo: I can't do this
right now", can be muted too. You can add any line you like.

Settings: `PlayerbotsPlus.Mute.*`.

## With other modules

- **[mod-ah-bot-plus](https://github.com/NathanHandley/mod-ah-bot-plus):**
  recommended alongside. It fills the auction house, and its prices are what
  the bots undercut. Its buyer buys the bots' auctions as it buys players'.
- **[mod-ollama-chat](https://github.com/kamakazedan/mod-ollama-chat)**
  (branch `dangaming`): when it's in the same build, the bots talk about what
  this module has them do.
  - They mention their trading in General: what sold, what they bought, what
    came back unsold.
  - A bot hanging out knows where it is and what it's doing when someone asks.
    An AFK one stays quiet until someone talks to it, then it's back.
  - A bot that was stuck on its mount knows it, and may laugh it off.
  - A rogue that can't sap the star says why in party.
  - It says much of the rest in its own words too: "Staying", "Following",
    hello when it joins a group, a quest objective done. Mute playerbots' own
    lines for those with the longer list in the conf's `MUTED BOT LINES`
    section.

  This works through the `PlayerbotsPlus.h` interface. Each module builds and
  runs without the other.
- **[mod-dungeon-clear](https://github.com/kamakazedan/mod-dungeon-clear):**
  while a run is on, it decides how its dead come back, and getting back up
  stays out of it.

### Coming from mod-ollama-chat

Hangouts, stuck mounts, getting back up, raid marks and muted lines used to be
part of mod-ollama-chat. They work the same here. Only the setting names have
changed, from `OllamaChat.*` to `PlayerbotsPlus.*`:

| Was | Now |
|---|---|
| `OllamaChat.Hangout.*` | `PlayerbotsPlus.Hangout.*` |
| `OllamaChat.Stuck.*` | `PlayerbotsPlus.Stuck.*` |
| `OllamaChat.Revive.*` | `PlayerbotsPlus.Revive.*` |
| `OllamaChat.Marks.*` | `PlayerbotsPlus.Marks.*` |
| `OllamaChat.Mute.*` | `PlayerbotsPlus.Mute.*` |

What the bots say about them stays in mod-ollama-chat, under its old names:
`OllamaChat.Hangout.StateText` and `AwayText`, `OllamaChat.Stuck.RememberMinutes`,
`RemarkChance`, `RemarkPrompt` and `StateText`, and `OllamaChat.Marks.SayEnable`,
`SayCooldownSeconds`, `SayPrompt` and the four `Sap*` situations.

## Install

```bash
cd modules
git clone https://github.com/kamakazedan/mod-playerbots-plus.git
```

Copy `conf/mod_playerbots_plus.conf.dist` to your module config folder as
`mod_playerbots_plus.conf`, then rebuild. The module needs no SQL.

## Commands (GM)

| Command | What it does |
|---|---|
| `.pbp status` | What it's doing: the auction house, hangouts, stuck bots, revives, marks and muted lines |
| `.pbp visit [name]` | That bot, or the one selected, trades now, wherever it is, and says what it did |
| `.pbp reload` | Re-reads the config. `.reload config` does too |

`PlayerbotsPlus.Debug = 1` logs more of what the bots do, such as each bot
heading to a hangout and a rogue's sap step by step.

## Development

See `CLAUDE.md` for the rules. The tests run without a server: the pricing,
the text helpers, and checks that the conf matches the code.

```bash
bash tests/build.sh
```

## Licence

GPL v2, as AzerothCore.
