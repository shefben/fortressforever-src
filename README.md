<div align="center">
  <img src="https://avatars.githubusercontent.com/u/2388970" width="200" height="200">
</div>

# Fortress Forever 2013
This is an actively developed port of the game [Fortress Forever](https://store.steampowered.com/app/253530/Fortress_Forever/) on Source SDK 2013 using [@Nbc66](https://github.com/Nbc66)'s [SDK 2013 Community Edition](https://github.com/Nbc66/source-sdk-2013-ce) repository.

Fortress Forever is based upon Source SDK 2006, the original repository of the game can be found [here](https://github.com/fortressforever/fortressforever).

**Note:** This repository contains the **source code** of the game and **not the game files**, The repository containing the game files are found [here](https://github.com/fortressforever-2013/fortressforever).

# Bots

This fork adds a full AI bot to Fortress Forever. Bots are **fake clients** built
on Valve's NextBot framework — the same one TFBot runs on — so they are real
player entities driven by synthesised `CUserCmd`s. They can only do what a human
could do with a keyboard: no teleporting, no direct velocity, no clipping past
geometry. Everything below follows from that.

```
sv_cheats 1
bot_add                  // one bot, auto team and class
bot_add red sniper       // team and class if you want them
```

If the map has never been navmeshed, `nav_generate` first. That is the only step
that is genuinely slow, and it is once per map.

Two documents cover this in depth, both in `mp/src/game/server/ff/bot/`:

- **[`README_BOTS.md`](mp/src/game/server/ff/bot/README_BOTS.md)** — architecture,
  decision-making, navigation, and what to do when a bot does something stupid.
- **[`README_NAV_BUILDER.md`](mp/src/game/server/ff/bot/README_NAV_BUILDER.md)** —
  hand-authoring map knowledge in-game with the numpad.

## What they do

**All ten classes,** each with its own tactics rather than a shared shooting
routine. Engineers site, build, upgrade and repair sentries and dispensers, and
push forward once their nest is topped up. Snipers hold sightlines and switch to
melee at close range instead of trying to scope someone in their face. Spies
disguise, cloak, infiltrate and go for backstabs from the rear arc. Medics follow
and heal. Demomen lay pipe traps at defensive chokes and detpack enemy
chokepoints. Everyone cooks and throws their class grenade.

**Every game mode,** not just CTF. What kind of map this is gets derived from the
live objective registry — how many flags exist, which teams may touch them, who
owns the capture points — rather than from the map name, which is a naming
convention rather than data. CTF, attack/defend, control point, invade, hunted,
fortball and plain deathmatch each get the right behaviour, and an attack/defend
map that moves through phases is re-read as it goes.

**Objectives in the right order.** rock2's flag is behind a door the keycard
opens. dustbowl's second point doesn't exist until the first one falls. Bots
handle sequenced objectives without a hand-written prerequisite table for each
map — a keycard always outranks a flag, because a keycard exists in order to gate
something, and beyond that a bot that can't reach what it's going for notices and
moves on to the next thing.

**Offense and defense as a team-level quota.** Left to themselves every bot picks
the same answer — go and take the thing — and a team of eight attackers loses
every attack/defend map no matter how good its navigation is. So the mode sets a
defender quota and the bots best suited to holding ground fill it. Class
selection follows the same logic, so teams come out with a plausible composition
instead of nine soldiers.

**Map machinery.** Bots open doors, ride lifts and press the buttons that call
them. That last one matters more than it sounds: Source buttons are *used*, not
walked into — the engine traces from the player's eye and requires them to be
close and looking at it — so a bot that only mashes `+use` while walking forward
will never press one, and any map with a button-opened route is a hard stop.

**Environmental hazards.** rock2 fills with gas partway through the round, and
there is a suit that stops it killing you. Bots go and get it. FF has no
"you are being gassed" signal to read, so the damage itself is the signal: a hit
with environmental damage bits, or one nobody is credited with while standing in
a `trigger_hurt` volume, is the hazard being live. Works for lava pits, crushers
and electrified floors nobody anticipated, too.

**Squad awareness.** Bots broadcast enemy sightings and sounds to their team,
escort flag carriers, intercept enemy ones, chase down stolen flags, seek out a
medic when infected, and retreat when hurt — more or less readily depending on
whether their team is winning.

`ff_bot_difficulty` (default `1`) scales reaction time, aim error and how
aggressively class abilities get used.

## Navigation

Bots path on Source's standard nav mesh, with four FF-specific additions at
generation time that the stock sampler doesn't provide: spawn-point seeding,
doors forced open while sampling, ladder generation (stock Source builds none
outside Left 4 Dead), and underwater connections so a submerged tunnel whose exit
is a vertical shaft isn't an isolated island.

On top of the geometry sit three layers of *meaning*, all re-derived at every map
load so none of it can go stale:

| Layer | Derives | From |
|---|---|---|
| Entity tagging | spawn rooms, flags, capture points, resupplies | live entities |
| Lua objective bridge | what each goal entity *is*, and whether it's live right now | the map's own Lua, then model, then name |
| Auto-tagger | high ground, chokes, water, ladders, hazard volumes, lifts | geometry and entities |

The Lua bridge is worth a note. Almost nothing about an FF map's objectives is in
the BSP — Lua creates and drives all of it at runtime — and FF already contained
a fully wired event bus reporting exactly that, gated behind a check for a bot
framework that isn't loaded. Reading it fixed two long-standing bugs: capture
points are `trigger_ff_script` entities and every lookup in the bot code scanned
the wrong class, so no bot had ever been able to find one; and attack/defend
phase gates were invisible, so bots walked to objectives that had been removed.

It also identifies things the map script has no vocabulary to declare. Gas suits,
keycards and balls are classified by model, which is how a bot knows to go and
get the suit at all.

**Learned links.** Nav generation is imperfect on already-shipped maps. Bots
watch every player — human and bot — and record traversals between areas the mesh
thinks are unconnected. Three independent observations commits the connection and
saves it. Humans playing a map teach the bots its geometry.

## Authoring map knowledge

Some things no detector recovers: which battlement is the sniping position and
which catwalk merely happens to be high, which way to face from it, which wall a
demoman blows open versus shut, which corridor is worth a pipe carpet.

```
ff_manual_nav_builder 1
```

binds the numpad, draws every existing marker, and prints the key map. Walk or
noclip to a spot, face the way you want them to face, press a key. The marker is
written to disk and applied to the live mesh immediately — no reload, no save
step. Twenty-three marker types across three pages, including two that author a
missing nav *connection* rather than tagging an area.

Markers live in a text sidecar keyed on **world positions**, so they survive
`nav_generate`. That is the point: redoing map knowledge by hand after every mesh
regeneration is what stops people doing it at all.

## Diagnostics

Every navigation failure in this subsystem used to be invisible from outside —
"has no path at all" and "has a path it can't follow" look identical when you're
watching a bot walk into a wall. So:

| Command | Tells you |
|---|---|
| `ff_bot_nav_report` | Per-team spawn / exit / ladder / water counts. **Start here** |
| `bot_show_path` | Draws the path. A red box overhead means *no path* — a data problem, not an AI one |
| `ff_bot_gamemode_report` | Detected mode, defense quota, every bot's role and what it's given up on |
| `ff_bot_lua_report` | Every objective entity the bots can see, live or not |
| `ff_bot_diagnose` | One bot's full state |
| `ff_nav_validate` | Coverage, connectivity, attribute counts |

# Build Instructions

- ## Windows
  To be able to compile Fortress Forever 2013 on Windows, you will need to download **Visual Studio 2022** and install:
  * MSVC v143 - VS 2022 C++ x64/x86 build tools
  * C++ MFC Library for latest v143 build tools (x86 and x64)
  * Windows 11 SDK (10.0.22000.0)
<br><br>
  1. Clone this repository and run `.\creategameprojects.bat` or `.\createallprojects.bat` located in `mp\src`
      * This will generate the project files and solution files that are needed in order to compile the game.
  2. Open the generated `Game_FF.sln` or `Everything_FF.sln` using Visual Studio 2022.
  3. Switch the current configuration from `Debug` to `Release`.
  4. Run `Build Solution`.
      * The compiled binaries should automatically be copied to `mp\game\FortressForever2013`.

- ## Linux
  **Note:** These instructions were only tested on Ubuntu 22.04 (Jammy Jellyfish), but should work for most Debian-based Linux distributions.

  #### Before getting started, install the following packages:
  - `build-essential`
  - `gcc-9`
  - `gcc-9-multilib`
  - `g++-9`
  - `g++-9-multilib`

  To be able to compile Fortress Forever 2013 on Linux, you will need to do the following:
  1. Clone this repository and run `./creategameprojects` or `./createallprojects` located in `mp/src`
      * This will generate the makefiles that are needed in order to compile the game.
  2. Run `make -f Game_FF.mak` in `mp/src` and the source code will start compiling.
      * The compiled binaries would automatically be copied to `mp/game/FortressForever2013`.

# External content
- ### [Coplay](https://github.com/CoaXioN-Games/coplay)
- ### [Discord-RPC](https://github.com/discord/discord-rpc)
- ### [Lua (5.1.5)](https://www.lua.org/)
- ### [LuaBridge3](https://github.com/kunitoki/LuaBridge3)