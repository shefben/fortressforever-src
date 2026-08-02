# Manual Nav Designer

Hand-authoring bot map knowledge in-game, with the numpad.

Companion document: [`README_BOTS.md`](README_BOTS.md) explains how the bots and
the nav mesh work in general.

---

## Contents

1. [What this is for](#what-this-is-for)
2. [Quick start](#quick-start)
3. [The key map](#the-key-map)
4. [Marker types](#marker-types)
5. [Spawn rooms](#spawn-rooms)
6. [Authoring a missing connection](#authoring-a-missing-connection)
7. [Teams](#teams)
8. [Radius](#radius)
9. [Deleting](#deleting)
10. [The file](#the-file)
11. [Where it plugs in](#where-it-plugs-in)
12. [Command reference](#command-reference)
13. [Underwater tunnels and other connectivity problems](#underwater-tunnels-and-other-connectivity-problems)
14. [Troubleshooting](#troubleshooting)

---

## What this is for

`nav_generate` gives the bots **geometry**. It does not give them **intent**:

- which battlement is the sniping position, and which catwalk merely happens to
  be high
- which way a sniper standing there should be *looking*
- where an engineer *should* drop a sentry, and which way it should face
- which wall a demoman blows **open** to make a route, and which one they blow
  **shut** to deny one
- which pool of water has the way out
- which corridor is worth a pipe carpet
- which way to look when you're standing at that corner with nothing to shoot
- where two areas are adjacent in the world but not in the graph

Five layers already handle everything they can:

| Layer | Derives | From |
|---|---|---|
| `CFFBotTagger` | spawn rooms, flags, caps, resupplies | live entities |
| `FFBotLuaObjectives` | goal type and class, incl. gas suits and keycards | Lua declarations, then model, then name |
| `FFBotGameMode` | what kind of map this is, who attacks, who defends | the live goal registry |
| `CFFBotAutoTagger` | high ground, chokes, water, ladders, hazard volumes, lifts | geometry and entities |
| `CFFBotAnalyzer` | **structural chokepoints, main routes, overlooks, sniper perches, defensive posts, sentry ground, aim directions, breakable shortcuts, teleport links** | the nav graph, the visibility sets, and world entities |

What's left over is judgement about a specific map. That's what this tool is
for — and the list has got a great deal shorter.

> **Start by finding out what you don't have to place.** Run
> `ff_nav_analyze_report` and `ff_nav_visualize all` before authoring anything.
>
> The gas suit used to need placing by hand; it's classified by model now. Which
> team attacks and which defends comes from the registry. And as of the analysis
> pass, so do **sniper perches, defensive posts, sentry ground, aim directions
> and breakable walls** — the five things that used to be most of the work.
>
> Author what the detectors miss or get wrong, not what they already get right.

> **Every marker type now has something reading it.** Ten of the word-2
> attributes used to be write-only — placed, drawn, saved, and consumed by
> nothing. That's no longer true of any of them, so a marker you place will
> change behaviour. The table in
> [`README_BOTS.md`](README_BOTS.md#nav-attributes-reference) names the consumer
> for each one.

---

## Quick start

```
sv_cheats 1
ff_manual_nav_builder 1
```

That writes `cfg/ff_navbuilder.cfg`, execs it (on a listen server), puts the
**key map on screen**, and starts drawing every existing marker.

The on-screen panel sits down the left edge and shows the current page's nine
slots — each in its own marker colour — plus which type is selected, which team
you're authoring for, how many markers exist, and the modifier keys. It updates
live, so paging or cycling the team is visible immediately.

Authoring means looking at the world, and the console printout it replaces was
exactly the wrong place for it: it scrolled past the moment you turned the mode
on, and reprinting it with `ff_nav_builder_help` meant opening a console you
then had to close before you could place anything. `ff_nav_builder_keymap 0`
turns the panel off if you know the layout.

Then: walk or noclip to a spot, **face the way you want them to face**, press a
numpad digit. Done. The marker is written to disk immediately and applied to the
live nav mesh immediately — no reload, no save step.

Numpad digits 1–9 place from the **current page**; `KP_SLASH` cycles through
three pages of nine. `ff_nav_builder_help` prints all of them.

> If you're upgrading from an earlier build, run `ff_nav_builder_writecfg` once.
> The old cfg binds command names that no longer exist.

```
ff_manual_nav_builder 0
```

turns the overlay off and disables placement. **The binds stay put and stay
harmless** — every bound command refuses to act while the cvar is `0`, so
there's no "restore my binds" step to get wrong. That's also why the numpad is
the target: it's the one block of keys FF leaves alone by default.

> A server cannot rebind a client's keys — `cl_restrict_server_commands` blocks
> exactly that, and rightly so. What it *can* do is write a cfg and exec it
> through the shared console of a listen server, which is where authoring
> actually happens. On a dedicated server you'll be told to run
> `exec ff_navbuilder` on the client yourself.

---

## The key map

Digits **1–9 place from the current page**. There are three pages; `KP_SLASH`
switches between them.

| Key | Action |
|---|---|
| `KP_END`..`KP_PGUP` (1–9) | place slot 1–9 of the current page |
| `KP_INS` (0) | place the **selected** marker type again |
| `KP_DEL` (.) | delete markers where you're standing |
| `KP_SLASH` | next page |
| `KP_MULTIPLY` | cycle authoring team |
| `KP_PLUS` / `KP_MINUS` | next / previous type (all 23, ignores pages) |
| `KP_ENTER` | list all markers |

### The pages

| Slot | Page 1 — objectives & build | Page 2 — tactical | Page 3 — occasional & links |
|:---:|---|---|---|
| 1 | `sniper` | `detpackseal` | `resupply` |
| 2 | `sentry` | `pipetrap` | `nospawn` |
| 3 | `dispenser` | `aim` | `escape` |
| 4 | `detpack` | `defend` | `linkfrom` |
| 5 | `spawn` corner | `danger` | `linkto` |
| 6 | `door` | `jump` | |
| 7 | `flag` | `waterexit` | |
| 8 | `cap` | `lift` | |
| 9 | `gassuit` | `capneutral` | |

**Page 1 is exactly the layout the tool had before pages existed**, in the same
order — existing muscle memory carries over unchanged.

Placing from a slot also *selects* that type, so `KP_INS` repeats it without
having to remember which page you're on. Any type is always reachable by name
with `ff_nav_place <type>` regardless of page.

Rebind freely — `ff_nav_builder_writecfg` regenerates the default file.

### Placement is traced to the floor

Placement uses your origin traced straight down, so **noclip authoring lands the
marker on the ground bots will actually stand on**, not wherever the camera
happened to float. Fly to the ceiling of a room, press a key, and the marker
lands on the floor beneath you.

### Six types also record which way you're looking

`sniper`, `sentry`, `aim`, `pipetrap`, `defend` and `jump` capture your **eye
yaw** at the moment you place them.

For these, direction *is* the payload. "Stand on the battlement" and "stand on
the battlement watching the bridge" are different instructions, and only one of
them is expressible with a position. Stand where they should stand, look where
they should look, press the key.

The overlay draws a facing arrow and the angle for these; every other type
ignores yaw entirely.

---

## Marker types

| Type | Team? | Yaw? | Sets | Consumed by |
|---|:---:|:---:|---|---|
| `sniper` | | ✔ | `FF_NAV_SNIPER_SPOT` | `SniperLurk` tier 1 — preferred over anything the heuristics derive |
| `sentry` | | ✔ | `FF_NAV_SENTRY_SPOT` | `EngineerBuildSentrygun`, `DemomanDetpack`, path cost (non-engineers route around) |
| `dispenser` | | | `FF_NAV2_DISPENSER_SPOT` | `EngineerBuildDispenser` — preferred within 400u of the sentry |
| `detpack` | | | `FF_NAV2_DETPACK_SPOT` | `DemomanDetpack` — weighted to dominate the heuristics |
| `detpackseal` | | | `FF_NAV2_DETPACK_SEAL` | `DemomanDetpack` — **defenders only**; attackers reject it outright |
| `pipetrap` | | ✔ | `FF_NAV2_PIPETRAP` | `DemomanStickyTrap` — outranks its invasion-vector guess |
| `spawn` | ✔ | | `FF_NAV_SPAWN_ROOM_*` | Everything. See [Spawn rooms](#spawn-rooms) |
| `door` | | | `FF_NAV_DOORWAY` | Path cost (`+1500` instead of impassable), `HandleDoors` |
| `lift` | | | `FF_NAV2_LIFT` | `RideLift` — also auto-detected |
| `flag` | ✔ | | `FF_NAV_FLAG_*` | Objective states |
| `cap` | ✔ | | `FF_NAV_CAP_*` | Objective states, sniper scoring |
| `capneutral` | | | `FF_NAV2_CAP_NEUTRAL` | Game-mode detection, `ResolveObjective` |
| `gassuit` | | | `FF_NAV2_HAZARD_GEAR` | `FFBotHazard` — also auto-detected |
| `defend` | ✔ | ✔ | `FF_NAV2_DEFEND_*` | `ResolveDefendPosition` — the first place defenders look |
| `aim` | | ✔ | `FF_NAV2_AIM_HINT` | The aim driver, above every pre-aim heuristic |
| `resupply` | | | `HAS_AMMO`+`HEALTH`+`ARMOR` | `GetAmmo`, `GetHealth`, path cost discount |
| `waterexit` | | | `FF_NAV2_WATER_EXIT` | Path cost `×0.4`, **only while in water** |
| `jump` | | ✔ | `FF_NAV2_JUMP_SPOT` | `HandleMobility` — running duck-jump |
| `danger` | | | `FF_NAV2_DANGER` | Path cost `×4` |
| `nospawn` | | | `FF_NAV_NO_SPAWNING` | Bot spawn selection |
| `escape` | | | `FF_NAV_HUNTED_ESCAPE` | Hunted `STATE_VIP_RUN`, game-mode detection |
| `linkfrom` | | | *nothing — makes an edge* | A*, once paired. See [Authoring a missing connection](#authoring-a-missing-connection) |
| `linkto` | | | *nothing — makes an edge* | ...the other end of that pair |

### `detpack` vs `detpackseal`

Not the same instruction. `detpack` means *blow this open to make a route*;
`detpackseal` means *blow this shut to deny one*. Same position, opposite
behaviour.

FoxBot has carried this distinction since TFC
(`W_FL_TFC_DETPACK_CLEAR` vs `W_FL_TFC_DETPACK_SEAL`) and conflating them means
a demoman cheerfully opening the route the defence just paid to close.

### `gassuit` and `lift` are also auto-detected

You usually won't need to place these by hand:

- **`gassuit`** — the auto-tagger reads it from `FFBotLuaObjectives`, which
  classifies protective equipment by model. rock2's suit is
  `models/barneyhelmet_faceplate.mdl`.
- **`lift`** — detected from `func_train`, `func_plat`, `func_platrot`,
  `func_tracktrain`, `func_elevator`, and any `func_door` / `func_movelinear`
  whose move direction is more than 45° off horizontal.

Place them manually when the map does something the detector can't see — a
Lua-driven platform, a `trigger_push` column that behaves like a lift, hazard
gear with a custom model.

Markers whose meaning already had an `FF_NAV_*` bit **reuse it**, so every
existing consumer picks them up for free. A hand-placed `sentry` marker is
indistinguishable to `EngineerBuildSentrygun` from a mapper-tagged one, which is
the entire point.

### Two of them are conditional

`waterexit` and `detpackseal` do nothing until the bot is in the right state,
and that's deliberate rather than a limitation.

- **`waterexit`** is only discounted while the bot is wading or swimming. From
  dry land a water exit is just a piece of shoreline, and a permanent discount
  would drag routes toward the water for no reason.
- **`detpackseal`** only applies to a demoman assigned to **defense**. Which
  instruction applies — open this route or close it — is a property of what the
  bot has been told to do, not of the marker. An attacker rejects seal spots
  outright rather than scoring them low, because a demoman opening the route his
  own defence just paid to close is worse than one who does nothing. Check
  `ff_bot_gamemode_report` if you've placed one and nobody uses it.

### Hints, not orders

`sentry`, `dispenser`, `detpack`, `detpackseal` and `pipetrap` are
**suggestions**. The behaviours score them highly and still fall back to their
own geometric search if none is in range. You're steering, not scripting.

`aim` and `defend` are stronger — they're consulted *before* the heuristics that
guess at the same question, because they're answering it with knowledge those
heuristics don't have. Which of four exits a push actually comes through is a
fact about how a map is played, not about its shape.

### Colours

Every type has its own colour, matched by `ff_nav_visualize`:

| Colour | Type | Colour | Type |
|---|---|---|---|
| magenta | `sniper` | dark red | `detpackseal` |
| orange | `sentry` | burnt orange | `pipetrap` |
| yellow | `dispenser` | pale yellow | `aim` |
| red | `detpack` | sky blue | `defend` |
| blue | `spawn` | crimson | `danger` |
| white | `door` | violet | `jump` |
| pale gold | `flag` | cyan | `waterexit` |
| lime | `cap` | steel | `lift` |
| green | `gassuit` | light grey | `capneutral` |
| spring green | `resupply` | dark grey | `nospawn` |
| pink | `escape` | bright yellow | `linkfrom` |
| amber | `linkto` | | |

Each marker draws:

- a **solid box** in its colour
- a **vertical stalk**, so a marker on the floor below still catches your eye
  from a catwalk — which is where you author from
- a **ground cross** pinning the exact position, which the box only implies
- a **facing arrow and angle** for the six yaw types
- a **radius circle** if one is set
- the type name, and team where relevant

Depth-tested, so markers are occluded by geometry like anything else.
`ff_nav_builder_overlay 0` suppresses it without turning off placement.

---

## Spawn rooms

**Four `spawn` markers of the same team bound one room.** Place them at the
corners, on the floor.

```
ff_nav_builder_team 0        (or 2/3/4/5 to force a team)
walk to corner 1, KP_5
walk to corner 2, KP_5
walk to corner 3, KP_5
walk to corner 4, KP_5       ← room closes here
```

The console tells you `spawn corner 2 of 4 — 2 more to close this room` as you
go, and prints the resulting bounding box and area count when the set closes.

### How the volume is built

- **XY**: the axis-aligned bounding box of the four corners.
- **Z**: from 64u below the lowest corner to `ff_nav_builder_spawn_height`
  (default 256) above the highest.

You mark the floor, you get the room. Raise `ff_nav_builder_spawn_height` for
multi-storey spawns.

Every nav area whose centre falls inside that box gets the team's spawn-room
bit and joins the mesh's per-team spawn list.

### Order matters

Corners are consumed per team **in placement order**, four at a time: markers
1–4 are one room, 5–8 the next. Not clustered — clustering guesses, and guessing
wrong here silently mislabels a whole room.

A trailing partial set is reported loudly and does nothing:

```
[ff_nav_builder] red has 2 spare spawn corner(s) — a room needs exactly 4.
                 Place 2 more, or delete the spares.
```

### Why this matters more than any other marker

Spawn rooms seed the per-team **incursion distance** flood fill, which is the
single most useful derived quantity the bots have — it's how they know where the
front line is, which direction is "forward", where to retreat to, and whether a
sniping position is safe. A team whose spawn room isn't tagged has no incursion
data at all and degrades to wandering.

Closing a spawn set immediately recomputes spawn exits, incursion distances and
invasion vectors. No reload needed.

---

## Authoring a missing connection

Every other marker in this document **tags an area**. `linkfrom` and `linkto`
are the exception: they change the **graph**.

That distinction is the whole reason they exist. An area that's adjacent in the
world and unconnected in the mesh is invisible to A* no matter what attributes it
carries, because there's no edge for the cost model to price. No amount of
tagging fixes it. Bots either take an absurd detour or find no path at all and
stand around looking broken.

Nav generation produces these constantly:

- underwater tunnels the sampler didn't join
- a doorway that happened to be **shut** when `nav_generate` ran
- a drop off a ledge — traversable one way only, and not modelled at all
- a gap that needs a run-up
- a ramp or lip the sampler skipped

### How to place one

1. Stand where the bot leaves from. Press the `linkfrom` key (page 3, slot 4).
2. Walk to where it comes out. Press `linkto` (page 3, slot 5).

The pair closes and the connection goes live in the graph immediately, so you
can walk it to check you put it where you meant to:

```
[ff_nav_builder] link pair closed — 2 connection(s) now live.
```

Two connections, because pairs are **bidirectional by default**.

### One-way links

```
ff_nav_builder_link_oneway 1
```

Then a pair is a single edge, `linkfrom` → `linkto`, and nothing comes back.

Use it for drops. A ledge you can jump off but not climb back up is genuinely
one-way, and wiring the return trip sends bots off it expecting to walk back.

The cvar is read when the pair is **applied**, so set it before placing, or
change it and run `ff_nav_builder_reload`.

### Pairing rules

Pairs are taken **in file order** — each `linkfrom` claims the next `linkto`
after it. Same rule as spawn corners, for the same reason: proximity clustering
guesses, and a wrong guess here silently wires together two places that aren't
connected at all.

An odd one out is reported and does nothing:

```
[ff_nav_builder] 3 linkfrom marker(s) and 2 linkto — they come in pairs,
                 and the odd one out does nothing.
```

Both ends must land on nav areas within 256u, and they must be **different**
areas. If either fails you get a warning naming the pair.

If the mesh already connects the two, nothing happens and that's not an error —
a regenerated mesh learning the connection on its own is the outcome you wanted.

### Deleting one

Deletion works, with a caveat you get told about:

```
[ff_nav_builder] the connection those markers made is still live in the graph.
                 It goes away on the next map load.
```

Attributes can be wiped and re-derived. A graph edge can't: calling
`CNavArea::Disconnect` on a live mesh while `PathFollower`s hold segments through
it is how you get a bot walking a path whose next waypoint no longer exists.
`ff_bot_links_clear` has the same constraint for the same reason.

### This vs. learned links

[`FFBotLearnedLinks`](README_BOTS.md#learned-links) discovers the same kind of
connection automatically, by watching players move. The two complement each
other:

| | Learned links | `linkfrom` / `linkto` |
|---|---|---|
| Source | watching players walk it | you |
| Needs | 3 observations of somebody making it | one pass |
| Direction | one-way per observation | bidirectional unless you say otherwise |
| Covers routes nobody takes | no | yes |
| Deterministic | no | yes |

Learned links are a safety net. If a connection matters — it's the route to the
flag, or the only way out of the sewer — author it, don't wait for the net to
catch it.

Both survive `nav_generate`: markers because the sidecar stores world positions,
learned links because their file format was changed to do the same.

---

## Teams

`ff_nav_builder_team`:

| Value | Meaning |
|---|---|
| `0` | whichever team you're currently on (default) |
| `2` | blue |
| `3` | red |
| `4` | yellow |
| `5` | green |

`KP_SLASH` (or `ff_nav_builder_team_cycle`) cycles
`auto → blue → red → yellow → green → auto`, with the result on your HUD.

`ff_nav_place <type> <team>` overrides it for one marker.

Only `spawn`, `flag`, `cap` and `defend` use it. Everything else stores team `0`
and ignores it.

---

## Radius

`ff_nav_builder_radius` (default `0`) applies to **newly placed** markers.

- `0` — tag exactly the one nav area under the marker.
- `>0` — tag every area whose **centre** is within that distance.

Measured to area centres, not to the nearest point on the area. Measuring to the
nearest point makes one huge open-field area swallow every small radius you place
inside it, which is the opposite of what you mean by "50 units around here".

Radius is stored per marker, so changing the cvar doesn't retroactively alter
anything.

---

## Deleting

`KP_DEL` or `ff_nav_delete [radius]` removes **every** marker within
`ff_nav_builder_delete_radius` (default 96u) of where you're standing. Markers
sit on the floor and your origin is your feet, so a plain radial test is right
without vertical fudging.

### What happens under the hood

Attribute bits are additive and shared. An area tagged `SENTRY_SPOT` could have
got that bit from your marker, from a mapper's `nav_edit` pass in the `.nav`
file, or from the auto-tagger — there's no way to subtract one marker's
contribution without knowing which.

So deletion wipes every bit a marker can set and re-derives the whole lot from
scratch by re-running `CFFBotTagger::TagAreasFromEntities`, which is the full
derivation pass (entities → manual markers → spawn exits → incursion distances →
invasion vectors → heuristics). Exact, and at authoring scale instant.

`ff_nav_builder_clear confirm` nukes every marker on the map and removes the
file. It requires the literal word `confirm` because it's one letter away from
`ff_nav_builder_save` in autocomplete and there's no undo.

---

## The file

`maps/<mapname>.ffnavpoints`, plain text, one marker per line:

```
// Fortress Forever — manual bot nav markers
// map: ff_2fort
//
// Written by ff_nav_place / ff_nav_delete. Hand-editable:
//     <type> <team> <x> <y> <z> <radius> <yaw>
// team  0 = none, 2 = blue, 3 = red, 4 = yellow, 5 = green
// radius 0 = tag the one nav area under the marker
// yaw    degrees. Only meaningful for sniper / sentry / aim /
//        pipetrap / defend / jump; ignored by everything else.
//
// Four 'spawn' markers of the same team bound one spawn room,
// consumed in the order they appear below.
version 2
linkfrom 0 128.00 -960.00 -320.00 0.00 0.0
linkto 0 128.00 -1216.00 -128.00 0.00 0.0
sniper 0 -1024.00 512.00 64.00 0.00 90.0
sniper 0 -1024.00 -512.00 64.00 0.00 -90.0
sentry 0 640.00 128.00 -192.00 0.00 180.0
dispenser 0 720.00 96.00 -192.00 0.00 0.0
gassuit 0 -320.00 1408.00 -64.00 0.00 0.0
spawn 2 -2048.00 -256.00 32.00 0.00 0.0
spawn 2 -2048.00 256.00 32.00 0.00 0.0
spawn 2 -1536.00 256.00 32.00 0.00 0.0
spawn 2 -1536.00 -256.00 32.00 0.00 0.0
```

Written on **every** placement and deletion. Comments (`//` or `#`) and blank
lines are ignored; both LF and CRLF work. Unknown types and malformed lines are
warned about individually and skipped, not fatal.

**Version 1 files still load.** The `yaw` column was added in version 2; a v1
marker simply has no recorded facing, which changes nothing for the seventeen
types that ignore it.

`linkfrom` / `linkto` lines use the same record as everything else — they just
don't stamp an attribute, they make a graph edge. Their order in the file is what
pairs them, so **don't reorder them by hand** unless you mean to re-pair them.

Hand-edit it freely, then `ff_nav_builder_reload`. Reload wipes and re-derives
rather than stamping on top — a hand edit can *remove* markers as well as add
them, and stamping the survivors over the old tags would leave deleted ones live.

### Why a sidecar, and why positions

**Why not the `.nav` file:** `CFFNavMesh::OnServerActivate` wipes every
non-persistent attribute bit, and `CFFBotTagger` re-derives spawn/flag/cap from
live entities every map load. Anything written into those bits at author time
would be erased before a bot ever read it. The sidecar is re-applied *after* the
entity pass instead, which also means a manual marker can legitimately
supplement or override what the entities say.

**Why world positions, not area IDs:** area IDs are invalidated by
`nav_generate`. The whole reason this tool exists is that redoing map knowledge
by hand after every mesh regeneration is what stopped people doing it in the
first place. Regenerate the mesh as often as you like — the markers re-attach to
whatever area now sits under each position.

If a position ends up with no nav area within 256u, you get a warning at load:

```
[ff_nav_builder] marker 'sniper' at (-1024 512 64) has no nav area within 256u
                 — it does nothing.
```

Loud on purpose. A silently dead marker looks exactly like a bot ignoring you.

---

## Where it plugs in

```
CFFNavMesh::OnServerActivate
  ├─ clear non-persistent attributes (both words)
  ├─ FFNavBuilder::OnMapLoad()             ← read the sidecar
  ├─ CFFBotTagger::TagAreasFromEntities()
  │    ├─ entity scan → spawn / flag / cap / resupply
  │    ├─ FFNavBuilder::ApplyToMesh()      ← stamp markers
  │    │    ├─ per-area attribute markers
  │    │    ├─ ApplySpawnRegions()         ← four corners → one room
  │    │    └─ ApplyLinkMarkers()          ← linkfrom/linkto → graph edges
  │    ├─ CollectAndMarkSpawnRoomExits()
  │    ├─ ComputeIncursionDistances()
  │    ├─ ComputeInvasionAreas()
  │    └─ CFFBotAutoTagger::TagAllAreas()
  ├─ MarkDoorwayAreas()
  ├─ FFBotLearnedLinks::OnMapLoad()
  └─ FFBotGameMode::OnMapLoad()
```

**After** entities so markers can override them. **Before** spawn-exit collection
and incursion distances so a hand-drawn spawn room participates in them exactly
like an entity-derived one — and so an authored connection is in the graph before
the incursion flood fill walks it, which is what makes a hand-linked sewer count
as a route to the enemy base rather than a dead end.

Round restarts re-run the tagger, so markers survive round transitions.

---

## Command reference

### Cvars

| Cvar | Default | Meaning |
|---|---|---|
| `ff_manual_nav_builder` | `0` | Master switch. Writes + execs the cfg, enables placement, draws the overlay |
| `ff_nav_builder_overlay` | `1` | Draw markers while build mode is on |
| `ff_nav_builder_team` | `0` | Team for team-specific markers; `0` = your team |
| `ff_nav_builder_radius` | `0` | Radius for newly placed markers |
| `ff_nav_builder_delete_radius` | `96` | How close a marker must be for `ff_nav_delete` |
| `ff_nav_builder_spawn_height` | `256` | Height above the highest corner still inside a spawn room |
| `ff_nav_builder_link_oneway` | `0` | `0` = `linkfrom`/`linkto` pairs connect both ways, `1` = one way only. Read when the pair is applied, so set it before placing or run `ff_nav_builder_reload` |
| `ff_nav_builder_keymap` | `1` | Draw the numpad key map on screen. `0` = off; the console still has `ff_nav_builder_help` |

### Commands

| Command | Does |
|---|---|
| `ff_nav_place <type> [team]` | Place a marker at your feet |
| `ff_nav_place_slot <1-9>` | Place slot N of the current page (what the digit keys call) |
| `ff_nav_builder_place` | Place the selected type |
| `ff_nav_delete [radius]` | Delete markers near you |
| `ff_nav_builder_page [1-3]` | Show the current page, or switch to one |
| `ff_nav_builder_page_cycle` | Next page |
| `ff_nav_builder_next` / `_prev` | Cycle the selected type across all 23 |
| `ff_nav_builder_select <type>` | Select a type by name |
| `ff_nav_builder_team_cycle` | Cycle the authoring team |
| `ff_nav_builder_list` | List every marker, plus spawn-set completeness per team |
| `ff_nav_builder_save` | Force a write (normally automatic) |
| `ff_nav_builder_reload` | Re-read the file and re-derive. Use after hand-editing |
| `ff_nav_builder_clear confirm` | Delete everything, remove the file |
| `ff_nav_builder_writecfg` | Regenerate `cfg/ff_navbuilder.cfg` |
| `ff_nav_builder_help` | All three pages + full type list with descriptions |

All are `FCVAR_CHEAT`. Placement commands additionally refuse to act unless
`ff_manual_nav_builder` is `1`.

### Checking your work

```
ff_nav_builder_list           what you've placed
ff_nav_validate               marker counts, coverage, connectivity
ff_nav_visualize manual       every area any marker touched
ff_nav_visualize sentry       just the sentry hints
ff_bot_sniper_report          why the snipers picked what they picked
ff_bot_nav_report             per-team spawn / exit / ladder / water counts
```

---

## Underwater tunnels and other connectivity problems

An underwater tunnel isn't a property of an area. It's a **connection between two
areas** — an edge in the graph, not a node. Most markers in this tool tag areas,
so most of them can't express one.

Two that can: `linkfrom` and `linkto`. See
[Authoring a missing connection](#authoring-a-missing-connection). They're
option 4 below, and the right answer when a specific connection matters — but
they're the *last* resort, not the first, because the mesh being wrong in one
place usually means it's wrong in several.

Connectivity is handled by four mechanisms, in the order you should reach for
them:

### 1. `ConnectSwimmableAreas` — automatic, at generation time

This is the one that actually solves underwater tunnels. During `nav_generate`,
after normal sampling, every submerged area is checked against everything stacked
above it within 400u. If at least half the column between them samples as water,
nothing solid is in the way, and a hull trace along the swim line is clear, the
two areas are connected **both ways**.

That's what makes a tunnel whose exit is a vertical shaft reachable. The stock
generator only steps horizontally and climbs at most `ClimbUpHeight`, so such a
tunnel came out as an isolated island: areas existed, nothing linked them, and
pathfinding correctly reported no route.

> **If your `.nav` file predates this, it has no swim connections at all.** They
> are generation-time data — no amount of marker placement will create them.
> Re-run `nav_generate`. Same applies to ladders, which stock Source doesn't
> generate for FF at all (`BuildLadders()` is entirely inside `#ifdef TERROR`).

Check with `ff_bot_nav_report` — it prints ladder and water/underwater counts.

### 2. `FFBotLearnedLinks` — automatic, from play

If a connection is genuinely missing, swim it yourself. `FFBotLearnedLinks`
watches every player and records traversals between unconnected areas. After 3
independent observations the link is committed to the live graph and saved to
`maps/<map>.ffnavlinks`.

```
ff_bot_learn_links 2      // 2 = also log every commit
// swim the tunnel three times
ff_bot_links_report
```

Learned links are keyed by **world position** as of file format v2, so they now
survive `nav_generate` — that used to be their one disqualifying weakness, since
regenerating the mesh is exactly when the learned repairs were most needed.

They remain statistical: three observations, one direction per observation, and
only for routes somebody actually takes. A connection nobody walks is never
learned.

### 3. `nav_edit` — manual, explicit, lost on regeneration

Stock Source nav editing still works:

```
nav_edit 1
// look at the first area
nav_mark
// look at the second area
nav_connect
nav_save
```

This writes into the `.nav` file, so it's lost on the next `nav_generate`. Use
`linkfrom` / `linkto` instead unless you specifically want it baked into the
mesh.

### 4. `linkfrom` / `linkto` — manual, explicit, survives regeneration

The builder's answer, and the one to use for a connection that matters.
Deterministic, bidirectional by default, one-way with
`ff_nav_builder_link_oneway`, stored in the sidecar as world positions so
regeneration doesn't touch it. Full detail in
[Authoring a missing connection](#authoring-a-missing-connection).

### What the `waterexit` marker *is* for

`waterexit` marks **where to leave the water** — the foot of a ladder, a ramp, a
surfacing point. That's genuine map knowledge ("which end of this pool has the
way out") and a legitimate area property. It does **not** create a connection.

It's now consumed: a bot that's wading or swimming gets a ×0.4 path-cost
discount on water-exit areas, so it heads for the marked way out instead of
whichever stretch of wall happens to be nearest. From dry land it does nothing,
deliberately.

### Order of operations for a watery map

```
1. nav_generate           builds swim connections + ladders
2. ff_bot_nav_report      confirm ladders > 0, underwater > 0
3. ff_nav_validate        confirm spawn → enemy flag connectivity
4. swim any remaining gaps yourself (learned links)
5. linkfrom / linkto for anything still missing that matters
6. ff_manual_nav_builder 1, place waterexit / spawn / objective markers
```

Steps 1–5 are connectivity. Step 6 is intent. Don't reach for step 6 to fix a
step 1 problem — a marker on an unreachable area does nothing.

And don't reach for step 5 to fix a step 1 problem either. If you find yourself
placing more than a handful of link pairs, the mesh needs regenerating: hand-
wiring a graph the sampler should have built is a lot of work to arrive somewhere
worse than `nav_generate` would have got you for free.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| Numpad does nothing | `ff_manual_nav_builder` is `0`, or the cfg was never exec'd. Run `ff_nav_builder_writecfg` then `exec ff_navbuilder` |
| No key map on screen | `ff_nav_builder_keymap` is `0`, or `ff_manual_nav_builder` is. The panel only draws while build mode is on |
| Numpad places the *wrong* type | Wrong page. The on-screen panel shows which page you're on; `KP_SLASH` cycles |
| Numpad binds broken after an update | The cfg is generated, not versioned. Re-run `ff_nav_builder_writecfg` |
| Marker has no facing arrow | That type doesn't use yaw — only `sniper`, `sentry`, `aim`, `pipetrap`, `defend`, `jump` do |
| Facing is wrong | Yaw is captured once, at placement, from your eye angles. Delete and re-place looking the right way |
| `no nav area within 256u of you` | You're off the mesh. Move onto walkable nav, or regenerate to extend coverage |
| Spawn room did nothing | Fewer than 4 corners for that team (`ff_nav_builder_list` shows `INCOMPLETE SET`), or the box contains no nav areas — check the printed bounds |
| Spawn room caught too much | Corners too wide, or `ff_nav_builder_spawn_height` too tall for a multi-level area |
| Markers gone after `nav_generate` | They're not. Re-check with `ff_nav_builder_list` — if a marker warns "no nav area within 256u", the mesh no longer covers that spot |
| Engineers ignore your sentry hint | It's a hint. Check the area is reachable and not inside your own spawn; `ff_nav_visualize sentry` to confirm the tag landed |
| Snipers ignore your sniper marker | `ff_bot_sniper_report` — confirm the area shows `SNIPER_SPOT` |
| `gassuit` marker seems redundant | It probably is. The suit is auto-classified by model; `ff_bot_lua_report` will list it as `hazard-gear`. Only place one by hand if the map uses a custom model |
| Overlay invisible through a wall | By design. Noclip to it |
| Link pair did nothing | Both ends must resolve to nav areas within 256u and be *different* areas — the warning names the pair. Or the mesh already connects them, which is fine |
| Link works one way only | `ff_nav_builder_link_oneway` is `1`. It's read when the pair is applied, so change it and `ff_nav_builder_reload` |
| Deleted a link, bots still use it | Expected, and you were warned. A graph edge can't be pulled out from under a bot walking it. Next map load |
| `ff_nav_visualize` won't show my links | They set no attribute — they make an edge, not a tag. `ff_nav_builder_list` shows them; `nav_edit 1` draws the graph |
| Placed a `defend` marker, nobody stands there | Nobody's assigned to defense, or it's more than 2500u from the thing being defended. `ff_bot_gamemode_report` shows the quota and the roles |
| Placed a `detpackseal`, no demoman uses it | Seal spots are defenders-only by design. `ff_bot_gamemode_report` — if every demoman is on offense, none of them will touch it |
| Placed an `aim` marker, bots ignore it | It only applies with no threat and no recent last-known threat. Walk away and watch an idle bot standing on it |
| Bots still bad after lots of markers | Markers are intent, not connectivity. Run `ff_bot_nav_report` first — if a team's spawn has `exits=0`, no marker will help |
