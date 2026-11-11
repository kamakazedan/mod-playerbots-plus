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
| [Out of the fire](#out-of-the-fire) | Step out of a campfire, bonfire or brazier, burning or not, instead of standing in it |
| [Room to stand](#room-to-stand) | Step aside when they've stopped inside someone, instead of piling up on one spot |
| [Getting back up](#getting-back-up) | Release and run back when nobody in the group can resurrect them |
| [Raid marks](#raid-marks) | Crowd-control their own mark and sap the star |
| [Hunters' pets and companions](#hunters-pets-and-companions) | Name their hunter pets the way players do, and keep companions of their side and level |
| [Duels at the city gates](#duels-at-the-city-gates) | Gather outside the gates of Orgrimmar and Stormwind and duel each other there |
| [Muted bot lines](#muted-bot-lines) | Playerbots' stock whispers, like "Using [Hearthstone]", are dropped |
| [The economy ledger](#the-economy-ledger) | Nothing new: it counts what the bots' economy does, day by day, to decide by |
| [Bot dungeons](#bot-dungeons) | Take dungeon quests, queue through the dungeon finder and run the dungeon themselves, with nobody playing among them (needs mod-dungeon-clear) |

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

- **Where:** 30 spots in the capitals, Shattrath and Dalaran come set up.
  The cities people meet in have the most, room for about a hundred each in
  Orgrimmar and Stormwind and sixty in Dalaran:
  - Orgrimmar: in the Valley of Strength in front of the inn and the auction
    house, outside the bank, the open middle of it and just inside the gates,
    then the Drag, the Valley of Honor, outside the gates and the zeppelin
    tower.
  - Stormwind: the Trade District, outside the auction house and the bank,
    the Valley of Heroes, Cathedral Square, the Mage Quarter, the Dwarven
    District, Old Town, the harbor and outside the gates.
  - Dalaran, from level 68 and for both sides: outside both banks, Runeweaver
    Square, the fountain in the Eventide and Krasus' Landing, none of them in
    the Silver Enclave or Sunreaver's Sanctuary.

  You can add your own, or change how many bots each one holds. A city's
  spots fill evenly, the emptiest first.
- **Who:** idle random bots near a spot, of its zone and floor. A bot that is
  grouped, queued, questing, gathering, fighting or flying is left alone, and
  so is one a real player has targeted.
- **How they spend it:** on a free bench, mounted on their best mount, sat on
  the ground or just standing about, for 5 to 20 minutes. Now and then one
  goes AFK, and it shows `<AFK>`.
- **Keeping spots busy:** playerbots sends few bots to the capitals, so a
  spot short of bots gets some fetched from elsewhere. While someone is in
  the city, a fetched bot arrives at the nearest friendly inn, the way people
  hearth in: a few steps in front of the innkeeper where nobody stands, on
  foot and not AFK, and it sets off for its spot straight away. While nobody
  is, it's put down at the spot, and it goes back to where it came from when
  it's done, if nobody would see.
- **Letting go:** an invite, a trade, a duel, a queue or a fight lets a bot go
  at once, except a duel at the gates, which is what that crowd is there for.
  Switching hangouts off lets everyone go.

Nothing happens while no real player is online. Every place a bot stands is
checked to be on the spot's own floor, in sight of its middle, indoors or out
the way the middle is, reachable on foot, out of any fire and not where
anyone stands, so never under the city, on a roof, in the inn off the spot or
in a brazier. Orgrimmar has no benches, so bots there stand, sit on the
ground or stay on their mounts.
`.pbp status` shows how full each city is.

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

## Out of the fire

A campfire, a bonfire or a brazier by a city's guards is a trap the game
fires at whoever stands in it. Playerbots only steps out of one in a fight,
and only for a bot with a real player as its master, so a random bot that
stops by a guard to talk, or sits down to rest by a campfire, burns on the
spot for as long as it stays there.

- A random bot burned by one, out of any fight and not in a group, walks out
  of it: away from the middle to just past its edge, onto ground it can walk
  to, and not into the next fire.
- One hanging out at a city spot takes that as its new place there.
- One burned again within a minute is also sent off to do something else, so
  it doesn't walk straight back to the guard it was talking to.
- Most fires burn nobody at all: Orgrimmar's bonfires are only something to
  cook at. A bot standing in a fire anyone can see is one -- a campfire, a
  bonfire, a brazier, a stove, as far as its model reaches, and further for
  one on a mount, whose mount's head would be in the flames -- walks out of
  it the same way whenever a real player is about, and nothing puts anyone in
  one of those either.
- `.pbp fires` shows the fires around you, how near anyone may stand, and who
  is in one and why.

Settings: `PlayerbotsPlus.Fire.*`.

## Room to stand

Players who stop somewhere stand a step apart. Playerbots never minds where
it stops: a bot sent to an innkeeper walks to the innkeeper's own spot, a bot
with nothing to do stands wherever it last stopped, and a handful of them end
up on one spot, inside each other.

- A random bot that has stood still for 10 seconds inside someone, a player
  or an NPC, steps aside two to five yards onto free, walkable ground on the
  same floor, and turns back to face them.
- Of two bots on one spot, the one that got there last moves.
- A bot hanging out at a city spot takes the new place as its own.
- Pets, companions and people walking past don't count, bots in a group or a
  fight are left alone, and only bots near a real player are looked at.

Settings: `PlayerbotsPlus.Space.*`.

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

## Hunters' pets and companions

**Pet names.** Playerbots tames a beast for a hunter and leaves it called
what it is: "Boar", "Wind Serpent". Players rename theirs, mostly after
something they love or for a laugh: a cat called Aslan, a boar called
Porkchop, a worm called Graboid. A random bot's hunter does the same:

- The name comes from its pet family's list, now and then from the list any
  pet may have. Every family has its own list, and you can change them.
- It's renamed the way the game renames a player's pet: the same checks,
  its one rename, the name saved.
- About one hunter in seven never bothers and keeps the name it was tamed
  with.
- Your own bots' pets are left alone.

**Companions.** Most players carry a companion or two, and some have one
trotting along. Random bots near you do too:

- **What they own:** most own a common one or two, bought from their side's
  pet sellers: Orgrimmar's snakes, Silvermoon's dragonhawks, Elwynn's cats,
  the Exodar's moths. A few collect them, more as they level: Booty Bay's
  parrots, drops like the Sprite Darter, the Argent Tournament's pets, and
  now and then a rare or promotional one.
- **Only what fits their level:** each companion has the level where it's
  had, so a level 20 has a pet seller's cat or a Deadmines macaw, not
  something from Dalaran. A bot never has out one it couldn't have yet.
- **Out of earshot:** a bot's first companion earns it Can I Keep Him?,
  and the game announces that to everyone in /say range and to the bot's
  guild, so bots learn theirs when no real player is close enough to see
  it, and a bot with none yet waits until nobody real is online in its
  guild.
- **Holidays:** Winter Veil's snowman and reindeer, Hallow's End's
  squashling, Brewfest's wolpertinger and the rest come out far more while
  their holiday is on, and about half the bots that own any pick one up.
  Out of season they're rare.
- **Having one out:** about 30% of the bots near you have one out, decided
  again every half hour or so. One that's done with it puts it away.
- **Swapping:** in a stretch with nothing on, about a third of those with
  one out swap it for another they own, once.
- **Never** in a dungeon, raid, battleground or arena.

With mod-ollama-chat, a bot knows what its pet is called and what is
following it, and talks about it when someone does.

Settings: `PlayerbotsPlus.Pets.*`, `PlayerbotsPlus.Companions.*`.

## Duels at the city gates

On a real realm people duel in one place: just outside the main gate of
Orgrimmar or Stormwind. There's a crowd standing about, a pair or two going at
it at a time, and the rest watching and waiting for a go. Playerbots only
starts a duel through its old RPG strategy, which a realm on the new one never
runs, so bots never did. Now they do it there, and nowhere else:

- **Where:** the hangout spots outside the gates of Orgrimmar and Stormwind
  (`Duels.Spots`). They fill like any other spot, also with bots from just
  inside the city, and a bot fetched from elsewhere while you're about
  arrives at the city's inn and walks out through the gate.
- **When:** while you're within 150 yards of a spot, a duel starts there every
  half a minute or so, two at a time at most.
- **Who:** two of the crowd of the same side and within 5 levels, with their
  health and mana up and not AFK. A bot doesn't duel again for 4 minutes.
- **How:** one walks over and challenges the other, playerbots accepts, and
  they fight it out. With mod-ollama-chat they say gg. Then both get their
  breath back, eating or drinking if they like, and go back to their places.
- **You:** set `Duels.ChallengePlayerChance` and now and then one of them
  challenges you while you're standing about at the gates, at most once
  every 10 minutes. It's off by default.
- **Talked into one:** with mod-ollama-chat, two bots chatting in the city
  may talk themselves into a duel. Only two of a side within 5 levels, near
  enough to walk out to the gates (as far as the gates take their crowd
  from, `Hangout.ArrivalRange`), are ever offered it. When they've said
  their piece, both walk out to the gates, and once both are there one
  challenges the other as soon as there's room for another duel. If the
  crowd there is full, two of it head off to make room for them.
  (`PlayerbotsPlus::CanGoDuel` and `GoDuel` in `PlayerbotsPlus.h`.)

To add a spot, add a hangout spot just outside a gate and put its name in
`Duels.Spots`. A spot inside a city is refused, because the game allows no
duels there.

Settings: `PlayerbotsPlus.Duels.*`.

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

## The economy ledger

Before anything about the bots' economy changes (professions that craft, gear
that has to be earned, selling in trade chat), the numbers: what it does now,
day by day. The ledger changes nothing; it only counts. Only playerbots' random
bots count, not anyone's own.

- **Gold:** what comes in (looted, quest rewards, auction house mail, the
  rest) and goes out (auction house buys and deposits, the rest). Also what
  playerbots gives or takes outside the game's own payments: its top-up
  whenever it refreshes a bot, and level resets.
- **The auction house:** what bots list and who buys it (the AH bot, a
  player, another bot), what bots buy and from whom, and what comes back
  unsold.
- **What they get:** looted by kind (herbs, ore, leather, cloth, gems, gear by
  quality, junk), and how much of it from herbs, ore veins, chests and fishing.
  Also what they make (crafted or conjured) and buy from vendors.
- **Where their gear comes from:** handed out by playerbots, looted, a quest
  reward, the auction house, a vendor or made, and its item level.
- **What they use up:** food and drink, potions, elixirs, flasks, bandages,
  scrolls, oils and stones.
- **A look at all of them, every half hour:** gold held, item level by level,
  professions, who is in the dungeon queue, the trade goods they carry and
  what is in their banks, and who has what up for auction.

`.pbp economy [days]` prints it (the last 7 days unless you say), from the
console or SOAP as well as in game. Each finished day is also written to the
server log. The figures are kept in the characters database
(`playerbots_plus_ledger`, made by the SQL in `data/sql` or at startup) for 90
days. Settings: `PlayerbotsPlus.Ledger.*`.

## Bot dungeons

Random bots run dungeons on their own, the way players do: they take the
quests, queue through the dungeon finder, clear the dungeon, keep the loot and
hand the quests in. Off by default (`PlayerbotsPlus.BotDungeons.Enable`), and
only in a build with
[mod-dungeon-clear](https://github.com/kamakazedan/mod-dungeon-clear) (branch
`dangaming`), which does the clearing.

- **Dungeon quests.** Playerbots' own questing only takes quests one can do
  alone, and empties a full quest log starting with the ones it can't. With
  bot dungeons on, idle bots walk to the givers of normal dungeon quests on
  their continent and take the ones that suit them (their level, not grey, for
  a dungeon they haven't outgrown). A few quest log slots are kept free while
  a bot holds one, by dropping what a player would first: failed, grey, other
  zones, the lowest. A bot with one done walks to hand it in before it queues
  again.
- **Queueing.** Every half minute or so, a party is picked: a tank, a healer
  and three to do the damage, of one side and a level for the dungeon, those
  holding quests for it first. Each queues on their own, as a player does. If
  a player is matched in, it's their group from then on.
- **The run.** A group of bots the dungeon finder made, with no player in it,
  is taken on in its dungeon. Anyone left outside is brought in, playerbots'
  wander-about strategies (grind, rpg) come off for the run, and
  mod-dungeon-clear clears the dungeon, silently: there's nobody there to
  talk to (mod-ollama-chat keeps its bots quiet in such a run too).
- **When it fails.** A run ends when the whole party dies with nobody to rez,
  when nothing has moved forward for 10 minutes (no kill, no pack cleared, no
  fight going on, not getting closer to the next boss), when it's stuck on
  something for 2 minutes, or after 2 hours.
- **After.** Everyone is brought back to life and leaves the group, which takes
  them back where they queued from. They're unbound from the dungeon and get
  their usual strategies back. They queue again after a cooldown, a longer one
  after a failure. A dungeon whose runs keep failing is left alone for a while.
  A quest that 4 runs of its dungeon haven't finished is dropped, and so is one
  that goes grey.
- **Loose ends.** A bot that logs in inside a dungeon it's no longer grouped
  for (the server went down mid-run) is sent back out.

New parties wait while nobody is playing, and while the world tick averages
over 200 ms; there are 5 at most at once. `.pbp dungeons` shows what's going
on, including the tick. `.pbp dungeons send <dungeon>` queues a party for a
dungeon now, quest or no quest. Settings: `PlayerbotsPlus.BotDungeons.*`.

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
  - A hunter knows its pet's name, and any bot knows what companion is
    following it, when someone asks or coos over it. Duels get a gg, and two
    bots chatting in the city may talk themselves into one and walk out to
    the gates to fight it out.
  - It says much of the rest in its own words too: "Staying", "Following",
    hello when it joins a group, a quest objective done. Mute playerbots' own
    lines for those with the longer list in the conf's `MUTED BOT LINES`
    section. That list also drops the whispers about quests as you take and
    hand them in ("Quest Available: [Call of Fire]", "Can't take [Call of
    Fire]"), which nobody in a real group sends.

  This works through the `PlayerbotsPlus.h` interface. Each module builds and
  runs without the other.
- **[mod-dungeon-clear](https://github.com/kamakazedan/mod-dungeon-clear)**
  (branch `dangaming`):
  - While a run is on, it decides how its dead come back, and getting back up
    stays out of it.
  - It clears the dungeons of [bot dungeons](#bot-dungeons). Without it, bot
    dungeons aren't built in, and switching them on only gets a warning.

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
`mod_playerbots_plus.conf`, then rebuild. The one table it has, the economy
ledger's, is made by the core's database updater from `data/sql`, or by the
module at startup if the updater is off.

## Commands (GM)

| Command | What it does |
|---|---|
| `.pbp status` | What it's doing: the auction house, hangouts (and how full each city is), stuck bots, fires, room to stand, revives, marks, pets, companions, duels, muted lines and the ledger |
| `.pbp economy [days]` | The random bots' economy over the last days (7 unless you say) |
| `.pbp visit [name]` | That bot, or the one selected, trades now, wherever it is, and says what it did |
| `.pbp dungeons` | Bot dungeons: parties in the queue, runs going on, recent results, the world tick, dungeon quests |
| `.pbp dungeons send <dungeon>` | Queues a party of bots for that dungeon now (its dungeon finder id, or part of its name) |
| `.pbp reload` | Re-reads the config. `.reload config` does too |

`PlayerbotsPlus.Debug = 1` logs more of what the bots do, such as each bot
heading to a hangout, a rogue's sap step by step, and bots walking off to take
or hand in dungeon quests.

## Development

See `CLAUDE.md` for the rules. The tests run without a server: the pricing,
the text helpers, the ledger's words and sums, bot dungeons' choices (who goes,
grey quests, wings, a full quest log), and checks that the conf matches the
code.

```bash
bash tests/build.sh
```

## Licence

GPL v2, as AzerothCore.
