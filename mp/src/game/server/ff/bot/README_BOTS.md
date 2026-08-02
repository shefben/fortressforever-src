# Fortress Forever Bots & Navigation

How the AI works, where its map knowledge comes from, and what to do when it
does something stupid.

Companion document: [`README_NAV_BUILDER.md`](README_NAV_BUILDER.md) covers
hand-authoring map knowledge.

---

## Contents

1. [Quick start](#quick-start)
2. [Architecture](#architecture)
3. [How a bot decides what to do](#how-a-bot-decides-what-to-do)
4. [Game modes, objectives and roles](#game-modes-objectives-and-roles)
5. [How a bot moves](#how-a-bot-moves)
6. [Lifts, buttons and other machinery](#lifts-buttons-and-other-machinery)
7. [Environmental hazards](#environmental-hazards)
8. [The navigation data pipeline](#the-navigation-data-pipeline)
9. [Automatic nav generation](#automatic-nav-generation)
10. [Automatic map analysis](#automatic-map-analysis)
11. [Nav attributes reference](#nav-attributes-reference)
12. [Lua objectives and the entity classifier](#lua-objectives-and-the-entity-classifier)
13. [Incursion distances](#incursion-distances)
14. [Path cost](#path-cost)
15. [Learned links](#learned-links)
16. [Diagnostics](#diagnostics)
17. [Troubleshooting](#troubleshooting)
18. [Known gaps](#known-gaps)

---

## Quick start

```
sv_cheats 1
bot_add                  // add one bot, auto team / class
bot_add red sniper       // team and class if you want them
```

If the map has never been navmeshed:

```
nav_generate             // reloads the map, takes a while on big maps
ff_nav_validate          // did it cover everything?
```

`nav_generate` is the *only* thing that produces geometry. Everything else on
this page decorates that geometry with meaning.

---

## Architecture

The bots are built on Valve's **NextBot** framework, the same one TFBot uses.
The important pieces:

| Piece | What it is |
|---|---|
| `CFFBot` | The bot itself — a `NextBotPlayer< CFFPlayer >`, i.e. a real player entity driven by synthesised `CUserCmd`s |
| `ILocomotion` (`PlayerLocomotion`) | Converts "move toward this point" into button presses |
| `IBody` (`PlayerBody`) | Owns the view angles; everything aims through `AimHeadTowards` |
| `IVision` | Who can I see, who's a threat, where was he last |
| `IIntention` / `Behavior< CFFBot >` | The behavior tree |
| `PathFollower` | Follows a computed path along the nav mesh |
| `CFFNavMesh` / `CFFNavArea` | FF's subclasses of the stock nav mesh, holding FF attributes and per-team data |

Because a bot is a *fake client*, it can only do what a human could do with a
keyboard. There is no teleporting, no direct velocity setting, no cheating past
geometry. Everything below follows from that constraint.

---

## How a bot decides what to do

The behavior tree is shallow on purpose. Two layers:

### `CFFBotMainAction` — always running

Owns the **continuous** concerns, every tick, regardless of what the bot is
doing strategically:

- head aiming (`UpdateLookingAroundForEnemies`)
- firing (`FireWeaponAtEnemy`)
- weapon selection
- door handling (`HandleDoors`)
- button handling (`HandleButtons`)
- stuck recovery (`HandleStuckState`)
- mobility — bunny-hop, crouch-jump, authored jump spots (`HandleMobility`)
- swimming aim
- drowning response
- environmental-damage detection (`OnInjured` → `FFBotHazard`)
- **the movement arbiter** (`DriveMovementArbiter`)

Sub-actions handle strategy and pathing **only**. They never aim and never
fire. This matters: `PlayerLocomotion::FaceTowards` calls `AimHeadTowards` at
`BORING` priority every single tick, so anything that wants the bot to keep
looking at a threat has to refresh at equal or higher priority every tick. Only
MainAction is in a position to do that.

### `CFFBotCtfObjective` — the objective state machine

The name is historical. It began as a CTF state machine, and for a long time
that was all it was — which is why non-CTF maps produced bots that wandered.
It's now two layers with a clean split:

**Layer one: the states.** Things that are true regardless of what kind of map
this is, and that a generic resolver couldn't know. Re-evaluated every ~1.25s.

| State | When |
|---|---|
| `STATE_VIP_RUN` / `STATE_ESCORT_VIP` | Hunted — I'm the civilian, or my team has one |
| `STATE_SEEK_CURE` | infected — find a friendly medic |
| `STATE_RETREAT` | low HP and not carrying — fall back |
| `STATE_CARRY_FLAG` | I have it — run to my cap |
| `STATE_RETURN_OWN_FLAG` | our flag is dropped — touch it to return it |
| `STATE_INTERCEPT_CARRIER` | our flag is stolen — chase the carrier |
| `STATE_ESCORT_CARRIER` | teammate has the enemy flag — escort |
| `STATE_DEFEND_OWN_FLAG` | engineer building, or a defender's class-specific post |
| `STATE_DEFEND_AT_CAP` | sniper fallback when `SniperLurk` isn't running |
| `STATE_GRAB_FLAG` | **CTF only** — nearest grabbable enemy flag |

**Layer two: the mode resolver.** Everything else defers to
`FFBotGameMode::ResolveObjective`, which is one ladder for every game mode:

| State | When |
|---|---|
| `STATE_HOLD_GROUND` | the resolver returned a defensive post |
| `STATE_PUSH_OBJECTIVE` | the resolver returned something to take or capture |
| `STATE_WANDER` | no live objective at all — deathmatch, conc maps, surf maps |

The split is deliberate. Anything that depends on *this bot's situation* lives
in layer one; anything that depends on *the map* lives in layer two, in one
place, so adding a mode doesn't mean touching a state machine. See
[Game modes, objectives and roles](#game-modes-objectives-and-roles).

`STATE_GRAB_FLAG` is a fast path, not a special case: on CTF the resolver would
reach the same flag by a longer road. It is skipped on every other mode, because
"the nearest thing I can pick up" is exactly the wrong instinct on a map where a
keycard has to come first.

Below all this sit the tactical actions, which MainAction, the objective layer
and the class layer push onto the stack: `Attack`, `MeleeAttack`,
`RetreatToCover`, `SeekAndDestroy`, `MoveToVantagePoint`, `GetAmmo`,
`GetHealth`, `EscapeHazard`, `RideLift`, `DestroyEnemySentry`, `SniperLurk`,
`EngineerBuildSentrygun`, `EngineerBuildDispenser`, `EngineerMaintain`,
`MedicFollow`, `SpyInfiltrate`, `SpySap`, `SpyAttack`, `DemomanDetpack`,
`DemomanStickyTrap`, `Taunt`.

Suspend priority in `CFFBotMainAction::Update`, highest first:

1. `EscapeHazard` — outranks combat. A bot trading shots while the room fills
   with gas loses the trade and then dies anyway.
2. `RideLift` — before the combat gate, because a bot that stops to fight
   halfway onto a platform gets left behind by it.
3. `Attack` / `SpyAttack` — a visible threat outside hold range.
4. `GetAmmo` / `GetHealth` / demoman traps — only with no visible threat.
5. `HealTeammate` — medics.

`ff_bot_difficulty` (default `1`) scales reaction time, aim error and how
aggressively bots use class abilities.

---

## Game modes, objectives and roles

`ff_bot_gamemode.cpp`. Three things live here, and they are genuinely separate
problems that used to be one absent answer.

### Mode detection

Derived from the **live goal registry**, not from the map name and not from the
script name. Both of those are available and both are wrong: map names are a
naming convention rather than data, and the script tells you which base file it
included, not what state the round is in.

What the registry knows *is* the shape of the game: how many flags exist, which
teams may touch them, which teams own capture points. And it updates when Lua
changes its mind, so an attack/defend map that removes `cp2` and restores `cp3`
gets re-read rather than assumed.

| Mode | Detected when |
|---|---|
| `fortball` | any live `BALL` goal |
| `hunted` | any live `HUNTED_ESCAPE` goal |
| `invade` | a neutral flag (touchable by all) plus ≥2 team-owned caps |
| `ctf` | ≥2 teams own a flag **and** ≥2 teams own a cap |
| `attack-defend` | exactly one team owns a flag, or exactly one owns caps |
| `control-point` | neutral caps and nothing above fits |
| `deathmatch` | no live objectives at all |

Keycards are deliberately excluded from the flag count. A keycard is flag-shaped
but it is a *gate*, not a win condition, and letting it vote would make rock2
look like CTF.

On `attack-defend`, whoever owns the capture points is doing the capturing, so
they're the attackers; everyone else defends. `ff_bot_gamemode -1` is auto;
`0`–`7` force a mode.

Re-derived whenever `FFBotLuaObjectives` reports the live set moved, throttled
to once a second.

### Objective resolution

`FFBotGameMode::ResolveObjective` is one ladder used by every mode. It replaced
three separate fallbacks — grab a flag, walk to a cap, follow the HUD arrow —
none of which knew about each other or noticed when their pick was unreachable.

1. **Carrying something** → nearest cap we're allowed to use.
2. **Assigned to defense** → `ResolveDefendPosition` (below).
3. **Take a carriable** — keycard, then ball, then flag. Rank dominates
   distance.
4. **A capture point** — ours by preference, otherwise any live one.
5. **The map's own objective arrow** — Lua's `UpdateObjectiveIcon`, which is
   exactly what a human's HUD is telling them.
6. **Authored neutral capture ground** — `ff_nav_place capneutral`, for maps
   whose control points are brushwork the script never declares.
7. **Hold ground** — a control-point map between phases still has somewhere
   worth standing.

Everything is filtered through touch permissions (`touchflags` /
`disallowtouchflags`) and through the per-bot blacklist below.

The arrow sits at step 5, **after** the derived steps, not before. The derived
logic knows things the arrow doesn't — that we're carrying something, that our
flag is being run out of the base, that we're on 20 health. The arrow's value is
in the cases the derived logic has no answer for. `ff_bot_objective` prints what
the map is currently pointing *you* at.

### Sequencing, and how gating is detected

Objectives can be **ordered**. rock2's flag is behind a door the keycard opens;
dustbowl's `cp2` doesn't exist until `cp1` falls. A bot that only knows "walk to
the nearest live goal" stands at a locked door for the rest of the round.

There is deliberately **no hand-authored prerequisite table**. There is no way to
write one that covers 599 map scripts, and a wrong table is worse than none.
Instead there are two general signals:

**Rank.** A keycard outranks a flag, always. A keycard exists in order to gate
something — that is what a keycard *is* — so taking it first is correct without
knowing what it opens or where. This one rule is the whole of rock2's sequence.

**Failure.** Whatever the prerequisite is, *not being able to get there* is how
it manifests, and that is observable without knowing what it was. Two detectors:

| Signal | Threshold | Source |
|---|---|---|
| A* found no path to the objective | immediate | `CFFBotCtfObjective::Update` |
| Distance to the objective hasn't fallen | 25s | `NoteObjectiveProgress` |
| Stuck recovery reached stage 3 | ~4s stuck | `m_abandonGoalRequest` |

Any of them blacklists that objective **for that bot only**, for 45 seconds, and
the ladder moves on to whatever is next. Four blacklist slots per bot — enough to
walk down past every item class on any shipped map.

The blacklist is cleared **on respawn**. A door the keycard opens, a phase gate
that has since fallen, a lift that was at the other floor: all very likely
reachable now, and keeping the entry across a death is how a bot ends up
permanently refusing to go somewhere for a reason that stopped being true.

`ff_bot_objective_gating 2` logs every drop with its reason.

### Defensive posts

`ResolveDefendPosition`, in order:

1. Nav areas tagged `FF_NAV2_DEFEND_<myteam>` (`ff_nav_place defend`), within
   2500u of the thing being defended. Scored with a per-bot deterministic
   multiplier so several defenders **spread across the available posts** instead
   of stacking on the nearest.
2. Any team's defend post, for maps authored before teams were distinguished.
3. The nearest `FF_NAV_CHOKE` within 1500u of the defended thing, biased
   slightly toward the enemy approach — hold the lane, not the pedestal.
4. Two thirds of the way from the thing to the nearest enemy spawn threshold.
5. The thing itself.

### Roles

**A quota, not a preference.** Left to themselves every bot picks the same
answer — go and take the thing — and a team of eight attackers loses every
attack/defend map regardless of how good its navigation is.

| Mode | Defenders, as a fraction of the team |
|---|---|
| attack/defend, defending side | 75% |
| attack/defend, attacking side | 10% |
| CTF | 40% |
| control point, invade | 35% |
| fortball | 30% |
| hunted | 25% |
| deathmatch | 0% |

`ff_bot_defense_fraction` overrides the lot with a flat percentage; `-1` is auto.

Who fills the quota is decided by **class affinity plus proximity** to whatever
is being defended — the bot already standing in the flag room is the one to leave
there. Affinity is a statement about whether a kit rewards standing still:
engineer 3.0, HWGuy 2.5, sniper 2.0, demoman 1.5, pyro 1.0, soldier 0.8, medic
0.3, scout and spy 0.0, civilian −10.

Medics and spies that aren't picked as defenders become `support`, because both
have their own top-level Action (`MedicFollow`, `SpyInfiltrate`) that is neither
pushing nor holding.

A bot keeps a role for at least **15 seconds** (`FFBOT_ROLE_MIN_HOLD`) so a
death on the far side of the map doesn't reshuffle the whole team. The quota
converges within a few seconds of a mode change.

### Class selection

`FFBot_PickAutoClass` now asks `FFBotGameMode::PickClassForTeamNeed` first,
which counts the team's current defenders against what the mode wants for a team
one larger, then does a **weighted draw** over whatever the team's class limits
still permit:

| | Defense weights | Offense weights |
|---|---|---|
| engineer | 30 | 3 |
| HWGuy | 20 | 5 |
| sniper | 15 | 5 |
| demoman | 15 | 10 |
| pyro | 10 | 10 |
| soldier | 10 | 20 |
| scout | 2 | 20 |
| medic | 4 | 15 |
| spy | 0 | 12 |

The old "one engineer, one medic, otherwise uniform" logic is still there as the
fallback for when the mode layer declines — no `CFFTeam`, every class capped.

`ff_bot_gamemode_report` prints the detected mode, the per-team quota, every
bot's role, and how many objectives each has blacklisted.

---

## How a bot moves

This is the part that produces the most confusing bugs, so it's worth
understanding.

### Movement is view-relative

`PlayerLocomotion::Approach( goalPos )` does **not** set velocity. It takes
`goalPos - feet`, decomposes it against the bot's **current eye vectors**, and
quantises the result into one of 8 button combinations
(`IN_FORWARD`/`IN_BACK`/`IN_MOVELEFT`/`IN_MOVERIGHT`). Worst-case angular error
is 22.5°.

Consequences:

- **Where the bot looks changes how it walks.** A bot that snaps its view
  180° while pathing will press `IN_BACK` and walk backwards into a wall.
- `NextBotPlayer::Upkeep` resolves a simultaneous `IN_FORWARD + IN_BACK` as
  `IN_FORWARD` (it's an `else if`), so two systems fighting over movement in
  the same tick produce a silent, arbitrary winner.

### The movement arbiter

Because of the above, there is exactly **one** `Approach()` per tick, issued by
`CFFBotMainAction::DriveMovementArbiter`, which runs **last** after every other
per-tick concern has had a chance to publish an override.

Anything that wants to influence movement calls:

```cpp
me->SetMoveOverride( position, duration, "reason" );
```

Path-following goes through `FFBotHelpers::CanDrivePath( me, path )`, which:

- returns `false` while a move override is active (so the path follower stands
  down instead of fighting it)
- publishes the path goal via `NotePathGoal()` so the aim driver knows where
  the bot is trying to go
- claims the tick (`m_pathDrivenTick`) so the arbiter doesn't double-drive

If you add a new behavior, **never call `GetLocomotionInterface()->Approach()`
directly.** Use `CanDrivePath` or `SetMoveOverride`.

### Stuck recovery

`HandleStuckState` runs a four-stage escalation ladder. Progress is measured on
horizontal velocity normally, but on **total** velocity when on a ladder or in
water — otherwise a bot climbing perfectly well looks stuck.

Order of refusal, before the ladder escalates at all:

1. On a ladder → stand down entirely; the locomotor owns it.
2. `HandleDoors` → a door is being worked.
3. `HandleButtons` → a button is being worked.
4. Water-stuck → re-aim along the route rather than blacklisting the water.

### Doors

Doors are a first-class behavior, not an obstacle. `HandleDoors` walks into the
door, presses `+use`, and sets a move override so stuck recovery doesn't back
the bot out of the door's trigger volume. Bounded to a 4-second window.

### Authored jump spots

`HandleMobility` consumes `FF_NAV2_JUMP_SPOT`. On a marked area, moving at speed,
with the path goal more than 32u above us, the bot commits to a running
duck-jump. The locomotor won't work this out on its own: `PathFollower` jumps
when the *nav graph* says a connection needs one, and the reason a jump spot got
marked by hand is usually that the graph doesn't say so.

This is **not** conc-jumping or rocket-jumping. Those are separate movement
verbs the bot doesn't have, and pretending otherwise would send soldiers walking
off ledges. It clears the gaps a running jump can clear and no more. See
[Known gaps](#known-gaps).

---

## Lifts, buttons and other machinery

`ff_bot_ride_lift.cpp`. `ff_bot_use_lifts` (default `1`) is the switch for both;
`2` logs every transition.

### Why a lift needs its own action

A lift breaks two assumptions the path follower is built on:

- **A nav area's position is constant.** It isn't. The area sits in world space
  where the platform was at level init, and the platform moves out from under it.
- **Arriving somewhere is a matter of moving.** On a lift it's a matter of
  *waiting*, and every movement system in the bot treats "not moving" as a
  symptom to be corrected. Left alone, the stuck ladder escalates mid-ride and
  stage 2 back-tracks the bot straight off the platform.

FoxBot carried a per-waypoint `W_FL_LIFT` flag for exactly this, with a
tightened arrival tolerance and the comment *"some lifts are small (e.g. rock2's
lifts)"*. `FF_NAV2_LIFT` is the same flag.

`FFBotLift::IsLiftEntity` is the single definition of what counts, shared with
the auto-tagger — a lift the tagger recognises and the rider doesn't is a bot
that walks onto a platform it will never wait for. `func_train`, `func_plat`,
`func_platrot`, `func_tracktrain`, `func_elevator`, plus `func_door` and
`func_movelinear` **only when their move direction is mostly vertical**. A
horizontal door is a door; a vertical one is an elevator wearing a door's
classname, and FF maps use both spellings.

### The ride

| State | What it does |
|---|---|
| `CALL` | platform isn't here — find the call button and press it |
| `BOARD` | platform is here, we aren't on it — walk on |
| `RIDE` | hold position; end when the platform stops after real travel |

Bounded throughout: 30s overall, 6s of standing in a shaft with no visible call
button, and arrival requires ≥48u of vertical travel so a bot standing on a
parked platform doesn't think it's riding.

`IsPossible` requires more than a lift area on the route — `FF_NAV2_LIFT` comes
from a brush's bounding box and lands on plenty of areas *next to* a lift. Either
the platform isn't there (somebody has to call it), or it is and the route wants
a different floor.

### Buttons

This is the half that matters more.

Source buttons are **used, not touched**: `CBasePlayer::PlayerUse` traces out
from the eye and requires the player to be close and looking at the thing. A bot
that only presses `+use` while walking forward into geometry will never press
one, so any map with a button-called elevator — or a button that opens a door
twenty feet away — is a hard stop. And "hard stop" means the bot stands in the
corridor until the round ends.

`FFBotLift::WorkButton` does the small piece of deliberate behaviour that's
actually required: aim the head at it, walk to it through the arbiter if out of
range, press `+use` inside 56u, and hold position while pressing so the bot
doesn't drift out of use range between ticks. It reports the interaction as
progress, so the stuck ladder stands down.

`HandleButtons` hunts for one within 320u **only once the bot is genuinely
stuck** (stage ≥ 1) and only with line of sight. Both gates matter: a bot that
detoured to every button it passed would never arrive anywhere, and a button
through a wall belongs to a different room.

---

## Environmental hazards

`ff_bot_hazard.cpp`. `ff_bot_hazard_response` (default `1`); `0` restores the
old behaviour of standing in the gas until dead, `2` logs every classification.

rock2 fills with gas partway through the round. There's a suit that stops it
killing you and every human on the server knows to go and get one. FoxBot's bots
never did — TFC's suit was an undifferentiated `item_tfgoal` and `DMG_NERVEGAS`
was on its damage handler's ignore list.

Two of the three hard parts were already solved elsewhere:

- **What to run to.** `FFBotLuaObjectives` classifies the suit by model, which
  no map script could express — Omnibot's nine goal types have no way to say
  "protective equipment".
- **Where the danger is.** `CFFBotAutoTagger` stamps `FF_NAV2_HAZARD_ZONE` on
  every nav area overlapping a `trigger_hurt`.

### When

The missing piece. There is no engine-level "the map is filling with gas" signal
and no way to read the Lua schedule that switches one on.

So ask the damage. `INextBotEventResponder::OnInjured` carries the full
`CTakeDamageInfo` and fires for environmental damage exactly as it does for a
rocket. A hit counts as environmental when:

- its type bits include `DMG_NERVEGAS`, `DMG_RADIATION`, `DMG_POISON`,
  `DMG_ACID`, `DMG_SLOWBURN`, `DMG_SHOCK`, `DMG_ENERGYBEAM` or `DMG_PARALYZE`;
  **or**
- nobody is credited with it — no player attacker, not a buildable, not
  ourselves — **and** we're standing in an area already tagged
  `FF_NAV2_HAZARD_ZONE`.

`DMG_DROWN` is deliberately excluded: drowning already has an owner in
`CFFBotMainAction::Update`, which surfaces the bot, and routing a drowning bot to
a gas suit would be worse than what it does now.

That needs no schedule and no map knowledge, and it works for a hazard nobody
anticipated.

### What happens then

Exposure has to last **1 second** before the bot commits — brief contact with the
edge of something isn't worth abandoning the objective for, and the bot is
already walking out of it. After that, `CFFBotEscapeHazard` suspends everything
else and either:

- **fetches the gear** — nearest live, touchable `FFGOALCLASS_HAZARD_GEAR` within
  3000u, falling back to the nearest area tagged `FF_NAV2_HAZARD_GEAR` for maps
  whose gear is a brush or a button we can't classify; or
- **leaves** — nearest area that is neither `HAZARD_ZONE` nor `DANGER` nor an
  enemy spawn room.

If the gear is unreachable it falls back to leaving, which only needs a
neighbouring area. The action ends as soon as the damage stops — it deliberately
does *not* require the gear to have been picked up, because if the damage
stopped, whatever we did worked, and if it starts again `IsPossible` brings us
straight back. 25s hard timeout.

While the signal is live, `FF_NAV2_HAZARD_ZONE` areas become expensive in
`CFFBotPathCost` (×8 plus 500 flat), so the route out leaves the volume at the
first opportunity rather than cutting a corner through the far side of the room.

---

## The navigation data pipeline

Every map load, in this order:

```
0. Lua spawns its entities        Notify_GoalInfo → FFBotLuaObjectives registry
1. .nav file loaded               geometry + persistent attributes
2. CFFNavMesh::OnServerActivate
     ├─ clear all non-persistent attributes (both words)
     ├─ FFNavBuilder::OnMapLoad()      read maps/<map>.ffnavpoints
     ├─ FFBotLuaObjectives::OnMapLoad()   sweep for goals + classify
     ├─ CFFBotTagger::TagAreasFromEntities()
     │    ├─ info_ff_teamspawn → spawn-room tags
     │    ├─ FFBotLuaObjectives registry → flag / cap / resupply tags
     │    │    (both CLASS_INFOSCRIPT and CLASS_TRIGGERSCRIPT, live goals only)
     │    ├─ FFNavBuilder::ApplyToMesh()      stamp hand-authored markers
     │    ├─ CollectAndMarkSpawnRoomExits()
     │    ├─ ComputeIncursionDistances()      per-team flood fill
     │    ├─ ComputeInvasionAreas()
     │    └─ CFFBotAutoTagger::TagAllAreas()  heuristic + entity-derived tags
     ├─ MarkDoorwayAreas()
     ├─ FFBotLearnedLinks::OnMapLoad()        apply learned connections
     └─ FFBotGameMode::OnMapLoad()            derive the mode from the registry

3. Every frame, FFBotManager_Tick
     ├─ FFBotIntel::Tick()
     ├─ FFBotLearnedLinks::Update()
     ├─ FFNavBuilder::Tick()              authoring overlay
     ├─ FFBotLuaObjectives::Tick()        reconcile live goal state;
     │                                    re-tag when the objective set moves
     └─ FFBotGameMode::Tick()             re-derive mode; re-run the role quota
```

`FFBotGameMode::Tick` must run **after** `FFBotLuaObjectives::Tick`, which is
what marks the mode stale.

Step 0 matters: `Notify_GoalInfo` fires from `SetBotGoalInfo`, which Lua calls
while spawning entities — **before** the nav mesh is activated. The registry
must therefore be safe to populate with no mesh present, which is why
`OnMapLoad` reconciles rather than clears.

The ordering is deliberate and load-bearing:

- Manual markers apply **after** entities, so a marker can supplement or
  override what the entities say.
- Manual markers apply **before** spawn-exit collection and incursion
  distances, so a hand-drawn spawn room feeds them exactly like a real one.
- Heuristics run **last**, so they can use spawn/flag/cap tags as inputs.

Round restarts re-run the tagger (Lua sometimes moves goal entities at
round-start), so markers survive round transitions.

### What persists where

| Data | Lives in | Survives `nav_generate`? |
|---|---|---|
| Area geometry, connections | `.nav` | No — it *is* the output |
| `SNIPER_SPOT`, `SENTRY_SPOT`, `HUNTED_ESCAPE`, `NO_SPAWNING`, `UNBLOCKABLE` | `.nav` (`FF_NAV_PERSISTENT_ATTRIBUTES`) | **No** — regenerating wipes them |
| Hand-authored markers | `maps/<map>.ffnavpoints` (text, v2) | **Yes** — stored as world positions |
| Hand-authored nav *connections* | same file, `linkfrom`/`linkto` pairs | **Yes** — same reason |
| Learned links | `maps/<map>.ffnavlinks` (binary, v2) | **Yes** — keyed by world position since v2 |
| All `FF_NAV2_*` bits | nowhere — re-derived every load | n/a |
| Lua goal registry | nowhere — rebuilt from events + rescan | n/a |
| Detected game mode, bot roles | nowhere — re-derived from the registry | n/a |
| Objective blacklists | nowhere — per-bot, cleared on respawn | n/a |
| Everything else | derived at load | n/a |

This is why the manual builder uses a sidecar keyed on positions rather than
`nav_edit` attributes keyed on area IDs. Redoing map knowledge after every mesh
regeneration is what stops people doing it at all.

---

## Automatic nav generation

`nav_generate` runs Source's stock walkable-space sampler, plus four FF-specific
additions. All four are **generation-time**: they write geometry and connections
into the `.nav` file, and no amount of tagging afterwards can substitute for
them.

> Not to be confused with automatic *tagging*, which happens at every map load
> and decorates existing geometry with meaning. That's
> [`CFFBotAutoTagger`](#auto-tagger-thresholds), including the three
> [entity-derived passes](#entity-derived-auto-tagging-pass-5) that find hazard
> gear, hazard volumes and lifts.
>
> The distinction matters when something looks missing. **`nav_generate` never
> creates a spawn-room, flag, capture-point, sniper or resupply tag.** It writes
> geometry and connections into the `.nav` and nothing else. Every attribute is
> re-derived at map load from live entities and geometry, and none of it is
> persisted — which is what lets a Lua phase change move the tags mid-round.
> `ff_nav_visualize all` after a load is the quick way to see what landed.

### 1. Spawn seeding — `CFFNavMesh::AddWalkableSeeds`

FF has no `info_player_start`. Seeds come from `info_ff_teamspawn` instead.
Without this the sampler has nowhere to begin.

### 2. Doors are forced open — `OpenDoorsForGeneration`

Stock generation samples the world as it stands, so a closed spawn gate means
the sampler never walks through the doorway and the spawn room comes out as a
disconnected island. Every openable brush entity (`func_door`,
`func_door_rotating`, `func_movelinear`, respawn gates…) is opened before
sampling and restored afterwards.

### 3. Ladders — `BuildLaddersFromBrushContents`

**Stock `CNavMesh::BuildLadders()` is entirely inside `#ifdef TERROR`**, which
is not defined for FF. Before this was added, *every FF map had zero ladders*.

The replacement probes `UTIL_PointContents` for `CONTENTS_LADDER` above nav
areas, merges the hits into vertical columns, and calls `CreateLadder` for each.
Column de-duplication uses a `CUtlRBTree` — a linear search over hundreds of
thousands of probe columns would make generation appear to hang.

### 4. Underwater connections — `ConnectSwimmableAreas`

The generator only steps horizontally and climbs at most `ClimbUpHeight`, so a
submerged tunnel whose exit is a vertical shaft generates as an isolated island:
the areas exist, nothing links them.

Swimming has no jump-height limit. Two areas that overlap in plan view with
water between them are mutually reachable, so they get connected **both ways**
when all of the following hold:

| Condition | Value |
|---|---|
| Lower area centre is in `CONTENTS_WATER` or `CONTENTS_SLIME` | required |
| Vertical gap | `> StepHeight`, `<= 400u` |
| Column samples (8 evenly spaced) that are water | `>= 4` |
| Any sample in `CONTENTS_SOLID` | disqualifies |
| Hull trace along the swim line | must be clear |

The "at least half the column is water" rule is what lets a bot surface into a
room above the waterline while still rejecting two dry areas that merely happen
to be stacked (which is a fall, not a swim).

> **If your `.nav` predates these changes it has no ladders and no swim
> connections.** They are generation-time data. Re-run `nav_generate`.

### After generation

`ff_nav_generate_full` chains the whole thing and reminds you what fires on
reload. Tagging and validation happen automatically at level init; the only
manual step is checking the report.

---

## Automatic map analysis

`ff_bot_analyze.cpp`. Runs last in the tagging pipeline, at every map load,
because every pass consumes something the earlier stages produced. `ff_nav_analyze`
re-runs it without a reload; `ff_nav_analyze_report` shows what it found;
`ff_bot_analyze 0` turns it off entirely.

### Why it exists

The bots had three ways to learn what a map means: entity tags (exact, but only
knows what an entity said), shape heuristics (cheap, local and crude), and hand
authoring (accurate, and somebody has to do it for every map, forever).

Everything the heuristics couldn't derive fell to hand authoring, and the list
was long. Most of it turned out not to be map knowledge at all — it's a property
of the nav graph, or of the visibility sets `nav_generate` already computes, or
of world entities nothing was reading.

### Pass 1 — Topology

**Articulation points**, via Tarjan's algorithm, iterative so a corridor-shaped
graph of several thousand areas doesn't blow the stack. O(V+E).

A cut vertex is a node whose removal disconnects the graph. On a nav mesh that
is *precisely* a chokepoint: somewhere the map funnels through with no way
around.

Compare what it replaces. `FF_NAV_CHOKE` meant "this area is between 32 and 96
units wide" — true of every doorway, stairwell and corridor in the map, hundreds
of areas, most of them irrelevant, and blind to a choke that happens to be wide.
Cut points are structural and have no tuning constant at all.

Results set `FF_NAV2_CUTPOINT` **and** `FF_NAV_CHOKE`, so the existing choke
consumers — HWGuy hold positions, sticky traps, path cost — start seeing real
chokepoints without any of them changing.

Spawn-room interiors are skipped: a spawn room is trivially a cut point because
it hangs off the map by its door, and saying so is useless.

### Pass 2 — Traffic

For every (spawn threshold, objective) pair, path between them and increment a
counter on each area the route crosses. Normalise by the busiest area; store on
`CFFNavArea::GetTrafficScore()` as 0..1. Areas above
`ff_bot_analyze_traffic_threshold` (0.35) get `FF_NAV2_HIGH_TRAFFIC`.

"Where does everyone actually walk" is upstream of most authoring decisions:
where a sentry earns its keep, where a pipe carpet catches somebody, which
corridor is worth watching, which of two ledges overlooks anything.

The cost function here is deliberately **not** `CFFBotPathCost` — that model is
per-bot and full of transient terms (combat intensity, one bot's recent stuck
position, its route seed, its class). We want the map's shape, not one bot's
opinion of it on one frame.

### Pass 3 — Visibility

`nav_generate`'s analyze phase already computes, for every area, the set of
areas it can see, and writes it into the `.nav`
(`nav_generate.cpp:4208`, `:4222`). Nothing was reading it.

With traffic from pass 2, "is this a good sniper perch" stops being a shape
heuristic and becomes a measurement: which areas see the most of the ground
people walk on, from far enough away to matter (700–4000u), while being
somewhere `IsAwayFromInvasionAreas` says the enemy isn't already standing.

The same data gives an **aim hint** free. If you know which visible area carries
the most traffic, you know which way to look. The yaw is stored on the area
(`CFFNavArea::SetAimYaw`), which is also where a hand-authored `aim` marker puts
it — so the consumer never has to know which kind it got.

> **This pass needs an analyzed `.nav`.** If yours predates the visibility
> phase, you get a loud warning and no overlooks, perches or aim hints. Re-run
> `nav_generate`.

### Pass 4 — Defense

A defender wants to be somewhere attackers must pass, with sight of it, close
enough to the thing being defended, and on our side of it. All four are now
measurable:

| Requirement | Source |
|---|---|
| must pass | `FF_NAV2_CUTPOINT` or `FF_NAV2_HIGH_TRAFFIC` (passes 1, 2) |
| sight of it | visibility sets (pass 3) |
| close enough | distance to the objective area, ≤2000u |
| our side | incursion distance vs. the objective's |

Output goes into the same `FF_NAV2_DEFEND_<team>` bits the hand-authored posts
use, so `ResolveDefendPosition` picks them up unchanged — and an authored post
still wins, because it's checked first.

A cut point in our own half with a sightline is also where a sentry does its
work, so those get `FF_NAV_AUTO_SENTRY_SPOT`.

### Pass 5 — Entities

Before this the bot layer read **six** world classnames. The BSP has far more
that maps onto gameplay knowledge.

**Breakable walls.** "Which wall does a demoman blow open" was the flagship
example of knowledge only a human had. It isn't. A breakable is a shortcut
exactly when destroying it shortens a path, and that's a graph query: take the
nav areas either side, path between them as things stand, and compare. No route
at all, or a detour over 1200u and more than 1.6× the straight line, means the
wall opens something. Anything else is scenery or a window. Sets
`FF_NAV2_BREACHABLE` and feeds `FF_NAV2_DETPACK_SPOT`.

**Teleports.** A `trigger_teleport` moves a player somewhere no amount of
walkable-space sampling will ever find — the two ends may be opposite corners of
the map. The destination is a named entity, looked up exactly the way
`CTriggerTeleport::Touch` looks it up, so this is a free, exact nav connection.
Added **one-way**, because a teleport is.

**Push volumes.** `trigger_push` is movement the mesh can't see. Tagged
`FF_NAV2_PUSH`.

### Hand authoring still wins

Every pass checks whether a stronger source already spoke. A derived aim yaw
never overwrites an authored one; a derived sentry hint never overwrites a
placed one; `ClearDerived` only wipes bits this module owns outright and
deliberately leaves anything the manual builder can also set. The analyzer fills
the map in; it doesn't argue with you.

### Cost

One pass over the graph, a few hundred A* runs, and a sampled visibility scoring
loop, all at map load. The report prints wall-clock time. If it's ever a
problem, `ff_bot_analyze 0`.

---

## Nav attributes reference

`CFFNavArea` carries two attribute words.

### Word 1 — `FF_NAV_*`

| Bit | Source | Meaning |
|---|---|---|
| `BLOCKED` | runtime | area currently blocked |
| `SPAWN_ROOM_{BLUE,RED,YELLOW,GREEN}` | entity / manual | team spawn room |
| `SPAWN_ROOM_EXIT` | derived | spawn area with a non-spawn neighbour |
| `HAS_{AMMO,HEALTH,ARMOR,GRENADES}` | entity / manual | pickup here |
| `FLAG_{BLUE,RED,YELLOW,GREEN}` | entity / manual | flag rest position |
| `CAP_{BLUE,RED,YELLOW,GREEN}` | entity / manual | capture point |
| `SNIPER_SPOT` | mapper / manual | **persistent** — hand-tagged sniper position |
| `SENTRY_SPOT` | mapper / manual | **persistent** — hand-tagged SG position |
| `HUNTED_ESCAPE` | entity / manual | **persistent** — VIP escape |
| `NO_SPAWNING` | mapper / manual | **persistent** — don't spawn bots here |
| `UNBLOCKABLE` | mapper | **persistent** — cannot be blocked |
| `WATER` | heuristic | feet-level water (wading) |
| `UNDERWATER` | heuristic | midbody+ water (must swim) |
| `CHOKE` | heuristic | narrow area between larger ones |
| `HIGH_GROUND` | heuristic | elevated vs. local neighbourhood |
| `AUTO_SNIPER_SPOT` | heuristic | high ground with LOS to enemy ingress |
| `AUTO_SENTRY_SPOT` | heuristic | near our flag with LOS down a corridor |
| `NEAR_LADDER` | heuristic | adjacent to a ladder |
| `DOORWAY` | derived / manual | overlaps an openable blocker |

`DOORWAY` deserves a note. `CNavArea::IsBlocked` goes true while a door is shut,
and the path cost used to treat any blocked area as impassable. That deleted the
*only* route out of a spawn room every time the gate closed — no path existed at
all, so bots just milled about inside. A doorway area is now merely expensive
(`+1500` flat), so a route still exists and `HandleDoors` gets a chance to open
the thing.

### Word 2 — `FF_NAV2_*`

Mostly hand-authored, from `maps/<map>.ffnavpoints`; three bits are also derived
from live entities. Never written to the `.nav`. Word 1 had one free bit and six
concepts wanted one, so rather than renumber an enum already present in shipped
`.nav` files, new attributes went into a second word with its own accessors.

Every bit now has a consumer.

| Bit | Source | Meaning | Consumed by |
|---|---|---|---|
| `DISPENSER_SPOT` | manual | build a dispenser here | `EngineerBuildDispenser` |
| `DETPACK_SPOT` | manual | breakable wall — blow it **open** | `DemomanDetpack::PickTargetChoke` (+20000) |
| `DETPACK_SEAL` | manual | breakable wall — blow it **shut** | `DemomanDetpack::PickTargetChoke`, **defenders only** |
| `PIPETRAP` | manual | demoman pipe-carpet position | `DemomanStickyTrap::OnStart`, outranks the invasion-vector guess |
| `AIM_HINT` | manual | look this way from here | `UpdateLookingAroundForEnemies`, above the pre-aim heuristics |
| `DANGER` | manual | author says stay out | path cost (×4) |
| `HAZARD_ZONE` | **auto** | overlaps a `trigger_hurt` | `FFBotHazard` classification, `FindWayOut`, path cost **while suffering** |
| `LIFT` | **auto** + manual | area rides a moving platform | `CFFBotRideLift` |
| `HAZARD_GEAR` | **auto** + manual | gas suit / protective equipment | `FFBotHazard::FindHazardGear` |
| `CAP_NEUTRAL` | manual | unowned capture point | mode detection, `ResolveObjective` |
| `JUMP_SPOT` | manual | jump launch position | `HandleMobility` — running duck-jump |
| `WATER_EXIT` | manual | where to leave the water | path cost (×0.4 **while in water**) |
| `DEFEND_{BLUE,RED,YELLOW,GREEN}` | manual + **derived** | hold this ground on defense | `ResolveDefendPosition` |
| `MANUAL` | manual | set on any area a marker touches | diagnostics |
| `CUTPOINT` | **derived** | removing this splits the nav graph | also sets `FF_NAV_CHOKE`; pass 4 |
| `HIGH_TRAFFIC` | **derived** | on a large share of spawn→objective routes | passes 3 and 4 |
| `OVERLOOK` | **derived** | sees a lot of the ground people walk on | feeds `FF_NAV_AUTO_SNIPER_SPOT` |
| `BREACHABLE` | **derived** | a breakable here opens a real shortcut | feeds `FF_NAV2_DETPACK_SPOT` |
| `TELEPORT` | **derived** | `trigger_teleport` mouth or exit | a one-way nav connection is added |
| `PUSH` | **derived** | `trigger_push` volume | diagnostics |

Rows marked **derived** come from [`CFFBotAnalyzer`](#automatic-map-analysis).
`AIM_HINT` and `DETPACK_SPOT` are now dual-sourced too — an authored marker
always wins, because the analyzer only writes where nothing already had.

`DANGER` and `HAZARD_ZONE` look similar and are not the same thing. `DANGER` is
an author saying "stay out"; `HAZARD_ZONE` is the map saying it, derived from
the entity. The distinction shows up directly in the cost model: `DANGER` is
priced all the time, because an author asserting a hazard is asserting a cost.
`HAZARD_ZONE` is priced **only while a bot is actually taking environmental
damage**, because plenty of maps have a `trigger_hurt` over a bottomless pit the
nav never touches and plenty more have one that's inert all round — the geometry
is always worth knowing, and whether it's dangerous right now is a separate
question with a separate answer.

`WATER_EXIT` is conditional for the same kind of reason: from dry land a water
exit is just a piece of shoreline, and discounting it permanently would drag
routes toward the water for no reason.

`DETPACK_SEAL` is conditional on the bot's **role**, not on the marker. Which
instruction applies — open this route or close it — is a property of what the
bot has been told to do, so an attacker rejects seal spots outright rather than
merely scoring them low. A demoman opening the route his own defence just paid
to close is worse than one who does nothing.

> `HasAttributeFF2` is deliberately a separate function from `HasAttributeFF`.
> Passing an `FF_NAV2_` constant to the word-1 accessor would be a silent no-op
> that compiles cleanly.

### Auto-tagger thresholds

| Constant | Default | Used for |
|---|---|---|
| `NEIGHBORHOOD_RADIUS` | 1500u | how far "local" reaches for high-ground |
| `HIGHGROUND_DELTA` | 64u | above the neighbourhood median to count as high ground |
| `CHOKE_MIN_WIDTH` / `MAX_WIDTH` | 32u / 96u | width band that counts as a choke |
| `SENTRY_FLAG_RADIUS` | 1200u | how near our flag an auto-sentry spot must be |
| `COVER_TRACE_RANGE` | 200u | cover probing distance |
| `GEAR_RANGE` | 192u | how near hazard gear an area counts as "at" it |
| `HAZARD_PADDING` | 48u | slop around a `trigger_hurt`'s bounds |
| `LIFT_PADDING` | 32u | slop around a lift brush's bounds |

### Entity-derived auto-tagging (pass 5)

Three passes that read live entities rather than geometry. They live in the
auto-tagger so `ff_nav_autotag` can refresh them without a level reload, which
matters when you're iterating on a map.

**`TagHazardGear`** — protective equipment, from `FFBotLuaObjectives`'
model-based classification. Stamps `FF_NAV2_HAZARD_GEAR` within 192u. Warns
loudly if gear exists but sits on no nav area, because that's unreachable.

**`TagHazardZones`** — every `trigger_hurt` in the map, stamping
`FF_NAV2_HAZARD_ZONE` on overlapping areas. This is the generic answer to
rock2's gas, lava pits, crushers and electrified floors. The damage may be
switched on and off by Lua on a schedule we can't see, but the *volume* is a
real entity present from level load, so **where** the danger is is knowable even
when **when** it's dangerous isn't.

**`TagLifts`** — `func_train`, `func_plat`, `func_platrot`, `func_tracktrain`,
`func_elevator`, plus any `func_door` / `func_movelinear` whose move direction
is more than ~45° off horizontal. A horizontal door is a door; a vertical one is
an elevator wearing a door's classname, and FF maps use both spellings.

The test itself is `FFBotLift::IsLiftEntity`, in `ff_bot_ride_lift.cpp` next to
the behaviour that acts on the tag. One definition on purpose: a lift the tagger
recognises and the rider doesn't is a bot that walks onto a platform it will
never wait for.

`ff_nav_autotag` clears and rebuilds the geometric heuristics plus
`FF_NAV2_HAZARD_ZONE`. It deliberately does **not** clear `FF_NAV2_LIFT` or
`FF_NAV2_HAZARD_GEAR` — those bits are shared with the manual builder, which
`ff_nav_autotag` doesn't re-run, so wiping them would silently drop
hand-authored markers. Entity-derived word-1 tags still need a full reload.

---

## Lua objectives and the entity classifier

Almost nothing about an FF map's objectives is in the BSP. Flags, capture
points, keys, gas suits, AvD phase gates — Lua creates and drives all of it at
runtime. `FFBotLuaObjectives` is the bridge.

### The event bus

FF already contained the right event bus, fully wired and completely dead.
`ff_item_flag.cpp` and `triggers.cpp` call seven notifications at exactly the
right moments — `Notify_GoalInfo` fires from `SetBotGoalInfo`, which **Lua calls
as it spawns each entity** — plus pickup, drop, return, respawn, remove and
restore. Every one of them opened with `if(!IsOmnibotLoaded()) return;`, and
Omnibot isn't loaded, so the whole stream was discarded.

`FFBotLuaObjectives` taps it ahead of that gate and keeps a live registry:
goal type, class, team flags, active/inactive/removed, carried/dropped, home vs
current position. Reconciles at 4Hz, rescans `gEntList` every 5s as a safety
net, and re-derives nav tags when the live set moves (throttled to 1s).

### Two bugs this exposed

**Every capture point in FF is a `trigger_ff_script`.**
`includes/base_teamplay.lua:362` declares `basecap = trigger_ff_script:new(...)`,
so a cap is a `CFuncFFScript` (`CLASS_TRIGGERSCRIPT`), not a `CFFInfoScript`.
Every cap lookup in the bot code scanned `CLASS_INFOSCRIPT`, so
`FindOwnCapPoint` and `FindAnyCapPoint` returned NULL on every stock FF map for
the entire life of the bot code. `STATE_CARRY_FLAG` had no cap to run to.

**AvD phase gates were invisible.** `base_ad.lua` calls `flag:Restore()` /
`flag:Remove()` as the round changes phase. All three flags and caps were tagged
live from level init, so bots walked to objectives that, to a player, don't
exist yet.

### The declared vocabulary, and its ceiling

`Omnibot::BotGoalTypes` has nine values: `kNone`, four backpack types, `kFlag`,
`kFlagCap`, `kHuntedEscape`, `kTrainerSpawn`. That's the entire vocabulary Lua
has. It cannot say "gas suit", "keycard", "button" or "neutral cap".

Coverage is better than it looks, though — **573 of 599 shipped map scripts
declare goals** once `IncludeScript` inheritance is followed. rock2 is typical:
zero `botgoaltype` in its own file, but it includes `base_teamplay`, so its keys
inherit `kFlag` and its gasbuttons inherit `kFlagCap`.

### The classifier

For what the vocabulary can't express, `FFBotGoalClass` is a superset and gets
filled by inference. Priority: **declared > model > name.** A declared
`botgoaltype` is never second-guessed.

Prior art is FoxBot, which solved the identical problem for TFC by whitelisting
three model paths at map load (`dll.cpp`, `pfnKeyValue` interception):
`models/flag.mdl`, `models/keycard.mdl`, `models/ball.mdl`, plus a `"pack"`
substring for backpacks. Twenty years of TFC ran on that.

| Model | Class |
|---|---|
| `/flag/flag.mdl` | `FLAG` |
| `/keycard/keycard.mdl` | `KEYCARD` |
| `/ball/ball.mdl` | `BALL` |
| `barneyhelmet_faceplate.mdl`, `/hazmat`, `/gasmask` | `HAZARD_GEAR` |
| `/armour/armour.mdl` | `ARMOR` |
| `/cells/cell.mdl`, `boxbuckshot.mdl` | `AMMO` |
| `/grenades/` | `GRENADES` |
| `/backpack/backpack.mdl` | `BACKPACK` |

Then name keywords (`gas suit`, `keycard`, `key`, `flag`, `ball`, …) as a weaker
fallback, checked against both the Hammer targetname and the Lua name.

> **`models/items/healthkit.mdl` is deliberately absent** despite 60 occurrences.
> `includes/base.lua` declares it as the *default* model for every
> `info_ff_script`, so by the time it reaches C++ there's no way to distinguish
> "the author chose a healthkit" from "the author chose nothing". Classifying on
> it would label every unconfigured script entity in the game as health.

**FF's gas suit is identifiable where TFC's wasn't.** TFC's was an
undifferentiated `item_tfgoal`, which is why FoxBot never found it and its bots
simply died in rock2's gas (`bot_client.cpp:48` puts `DMG_NERVEGAS` on the
ignore list). FF gives the suit its own model.

Diagnostics: `ff_bot_lua_report` marks each entry `declared` or `INFERRED` and
counts the classes Lua can't name. Kill switches: `ff_bot_lua_objectives 0`,
`ff_bot_classify_entities 0`.

---

## Incursion distances

Per team, per area: the travel distance along the nav graph from that team's
spawn room(s) to this area. `-1` means unreachable from that team's spawn.

Computed by a BFS flood-fill from every spawn-room area outward, after tagging.
This is the single most useful derived quantity in the system, because it turns
"where is the front line" into arithmetic:

- **Higher enemy incursion** = deeper into their territory.
- `GetNextIncursionArea( team )` = the neighbour that pushes furthest forward.
- `CollectPriorIncursionAreas` / `CollectNextIncursionAreas` = retreat / advance
  candidates.
- `GetEnemyInvasionAreaVector( myTeam )` = the areas the enemy comes from.
- `IsAwayFromInvasionAreas( myTeam, range )` = is this a safe place to stand,
  used to score sniper vantage points.

**A team whose spawn room isn't tagged has no incursion data at all**, and
everything that depends on it degrades to wandering. That's why
`ff_bot_nav_report` leads with per-team spawn/exit counts.

---

## Path cost

`CFFBotPathCost` (in `ff_bot_path_cost.h`) is the `IPathCost` handed to
`NavAreaBuildPath`. It multiplies and adds:

| Signal | Effect |
|---|---|
| Ladder or elevator edge | **exempt from height checks entirely** |
| Height change beyond class jump/drop ability | impassable |
| `DOORWAY` while blocked | `+1500` flat, *not* impassable |
| `DANGER` (authored) | `×4` |
| `HAZARD_ZONE`, **only while taking environmental damage** | `×8` and `+500` flat |
| `WATER_EXIT`, **only while wading or swimming** | `×0.4` |
| `SENTRY_SPOT`, non-engineer/spy | `×1.5` |
| Resupply while low on ammo/health | `×0.6` |
| Combat intensity | `+ intensity × 200` flat |
| `WATER` | `×1.4` |
| `UNDERWATER` | `×2.0` |
| HWGuy / Soldier in water | `+20%` on top |
| Scout / Spy in water | `−25%` |
| Route flavour `DRY` | water `×1.4` extra |
| Route flavour `WATER` | water `×0.6` |
| Class: Scout | flat costs halved |
| Class: Soldier | flat costs `×0.7` |

The ladder/elevator exemption is critical.
`ComputeAdjacentConnectionHeightChange` returns `±FLT_MAX` for ladder edges, so
without the exemption every ladder in the game was rejected as an impossible
climb.

`DANGER` is expensive rather than impassable on purpose: an author marking a
hazard is describing a cost, not a wall, and turning it into a wall is how you
end up with no path at all through a corridor whose only route runs past the
thing.

`HAZARD_ZONE` — the auto-detected `trigger_hurt` volumes — is **conditional**,
and that condition is the whole design. Giving it a permanent cost would be
wrong: plenty of maps have a `trigger_hurt` over a pit the nav never touches, and
plenty more have one that's inert for the entire round because Lua never switches
its damage on. Penalising all of them would distort routing on every map for the
sake of the few where it matters, at the times it doesn't.

So the cost is gated on the only evidence the thing is live: **this bot is being
hurt by it right now**, as reported by [`FFBotHazard`](#environmental-hazards).
Then it's charged steeply — steeper than `DANGER`, because at that point it isn't
an opinion, something in the volume is measurably taking our health away and the
route out should leave at the first opportunity rather than cut a corner through
the far side of the room.

`WATER_EXIT` is gated the same way and for the same kind of reason: from dry land
a water exit is just a piece of shoreline, and a permanent discount would drag
routes toward the water for no reason. To a bot already *in* the water it's worth
a real discount, because the mesh knows the water connects to the shore but not
which stretch of shore you can actually climb out at — which on FF's sewers and
canals is frequently one specific ladder foot out of forty metres of wall.

**Combat intensity** replaces an older custom danger-score system. `OnCombat()`
spikes an area's score; `GetCombatIntensity()` reads it back with exponential
decay (`ff_nav_combat_build_rate` 0.05, `ff_nav_combat_decay_rate` 0.022).

---

## Learned links

`FFBotLearnedLinks` watches **every** player — human and bot — and records nav
connections the mesh is missing. Cheap: one nav lookup per moving player per
~64u travelled.

| Rule | Value |
|---|---|
| Sample distance | 64u |
| Minimum speed | 100 u/s |
| Minimum continuous movement before sampling | 2s |
| Maximum single step (teleport/push guard) | 400u |
| Observations before a link is committed | 3 |
| Write-back throttle | 30s |

Committed links go into `maps/<map>.ffnavlinks` and are re-applied at map load.

**File format v2 keys links by world position, not by area ID.** That change
matters: area IDs are assigned by nav generation and mean nothing against a
different mesh, so every link learned before a `nav_generate` used to be silently
discarded on the next load — the exact moment the mesh changed and the learned
repairs were most needed was the moment they all went away.

v1 files still load. Their IDs are resolved once against the current mesh and
re-keyed to positions on the next save, so an existing server upgrades without
losing anything, *provided its mesh hasn't changed in between*. If it has, those
links were already lost. Resolution tolerance is 128u, deliberately generous: the
whole point is that the mesh may have changed shape, and an area whose centre
moved sixty units is still the same place.

`ff_bot_learn_links`: `0` off, `1` learn and apply (default), `2` also log every
commit.

This is a safety net, not a substitute for a good mesh. If you find yourself
relying on it, the mesh needs regenerating — or the connection needs authoring
explicitly with `linkfrom` / `linkto`, which is deterministic where this is
statistical. See
[the nav builder](README_NAV_BUILDER.md#authoring-a-missing-connection).

---

## Diagnostics

### Nav

| Command | What it tells you |
|---|---|
| `ff_bot_nav_report` | Per-team spawn / exit / threshold / doorway / ladder / water counts. **Start here.** `exits=0` for a team means its spawn room is a disconnected island — the mesh is the problem, not the AI |
| `ff_nav_validate` | Entity coverage, spawn→flag connectivity, full attribute counts (both words) |
| `ff_nav_visualize <type>` | Draw areas carrying an attribute. `list` prints every type with its colour and meaning; `all` colours **every** tagged area by category and prints a legend with per-category counts; `combat` shows combat intensity. Note `linkfrom` / `linkto` markers set **no attribute** — they make a graph edge, so use `ff_nav_builder_list` and `nav_edit 1` to see those |
| `ff_nav_autotag` | Re-run heuristics without a reload |
| `ff_nav_generate_full` | `nav_generate` + reminder of what auto-fires on reload |
| `ff_bot_sniper_report` | Sniper-spot candidates and their scoring signals |
| `ff_bot_links_report` / `_clear` / `_save` | Learned links |
| `ff_bot_lua_report` | Every Lua goal entity the bots see, with live state and whether its class was declared or inferred |
| `ff_bot_lua_rescan` | Force a rescan + re-tag |
| `ff_bot_objective` | What the map is pointing YOU at (the HUD arrow target) |
| `ff_bot_gamemode_report` | Detected mode and what it was derived from, per-team defense quota, every bot's role, and how many objectives each has blacklisted. **The first thing to check when bots go to the wrong place** |
| `ff_bot_gamemode_rederive` | Re-derive the mode from the live registry now, ignoring the throttle |
| `ff_nav_analyze` | Re-run all five analysis passes on the current mesh, no reload needed |
| `ff_nav_analyze_report` | What the last analysis derived, pass by pass, with timings. Says plainly when a `.nav` has no visibility data or the map produced no traffic |

### Bots

| Command | What it tells you |
|---|---|
| `ff_bot_diagnose [name]` | Team, class, HP, position, velocity, threat, status effects, buildings |
| `bot_show_path [name]` | Yellow line = path goal segment. **Red box overhead = NO PATH** (the failure that looks like "stuck"). Cyan line = the movement arbiter is driving, with the reason. `STUCK stage N` = position in the recovery ladder |
| `bot_show_threat [name]` | Magenta line = current threat. Orange cross = last-known-position memory |

These exist because every navigation failure in this subsystem used to be
invisible from outside: "bot has no path at all" and "bot has a path it can't
follow" look identical when you're watching it walk into a wall.

### Authoring

| Command | Does |
|---|---|
| `ff_manual_nav_builder 1` | The marker tool. See [`README_NAV_BUILDER.md`](README_NAV_BUILDER.md) |
| `ff_nav_mark_sniper` / `ff_nav_unmark_sniper` | Tag the area you're standing in as a sniper spot, written to the `.nav` by `nav_save`. Predates the marker tool and still works — but it's keyed to the `.nav`, so `nav_generate` wipes it. Prefer `ff_nav_place sniper` |

### Cvars

| Cvar | Default | Meaning |
|---|---|---|
| `ff_bot_difficulty` | `1` | Reaction time, aim error, how aggressively class abilities are used |
| `ff_bot_learn_links` | `1` | Learn missing nav connections from player movement. `2` logs commits |
| `ff_bot_lua_objectives` | `1` | Track Lua goal entities and live state. `0` = level-init snapshot only. `2` logs state changes |
| `ff_bot_classify_entities` | `1` | Infer undeclared entities from model/name. `2` logs every inference |
| `ff_bot_gamemode` | `-1` | Force the game mode. `-1` auto, `1` CTF, `2` attack/defend, `3` control point, `4` invade, `5` hunted, `6` fortball, `7` deathmatch |
| `ff_bot_defense_fraction` | `-1` | Percentage of each bot team assigned to hold ground. `-1` picks from the mode |
| `ff_bot_objective_gating` | `1` | Blacklist objectives a bot can't reach so it falls through to the next. `2` logs every drop with its reason |
| `ff_bot_hazard_response` | `1` | React to environmental damage. `0` = stand in the gas until dead. `2` logs every classification |
| `ff_bot_use_lifts` | `1` | Ride moving platforms and press the buttons that call them. Also gates `HandleButtons`. `2` logs transitions |
| `ff_bot_analyze` | `1` | Derive gameplay knowledge from the graph, visibility and entities at map load. `0` = hand-authored markers only |
| `ff_bot_analyze_traffic_threshold` | `0.35` | Share of the busiest area's traffic needed to count as a main route. Lower tags more |
| `ff_bot_analyze_overlook_threshold` | `0.45` | Share of the best overlook score needed to count as one |
| `ff_nav_visualize_persist` | `1` | Keep the last `ff_nav_visualize` view drawn until switched off. `0` = draw once and expire |
| `ff_nav_builder_keymap` | `1` | Draw the numpad key map on screen while the builder is on |
| `ff_bot_ammo_search_range` | `5000` | How far a bot will go for ammo |
| `ff_bot_health_search_range` | `2000` | How far a bot will go for health |
| `ff_bot_retreat_to_cover_range` | `1000` | Cover search radius |
| `ff_bot_wait_in_cover_min_time` / `_max_time` | `1` / `2` | Seconds spent in cover before re-emerging |
| `ff_bot_melee_attack_abandon_range` | — | Give up a melee chase past this |
| `ff_bot_sniper_patience_duration` | `45` | Seconds a sniper holds a position before getting bored |
| `ff_bot_sniper_melee_range` | `300` | Switch to melee inside this |
| `ff_bot_spy_knife_range` | `300` | Backstab attempt range |
| `ff_bot_spy_change_target_range_threshold` | `300` | How much closer a new target must be to switch |
| `ff_nav_combat_build_rate` | `0.05` | Combat-intensity gain per `OnCombat()` |
| `ff_nav_combat_decay_rate` | `0.022` | Combat-intensity decay per second |

Builder cvars (`ff_nav_builder_*`) are documented in
[`README_NAV_BUILDER.md`](README_NAV_BUILDER.md#cvars).

---

## Troubleshooting

| Symptom | First check | Likely cause |
|---|---|---|
| Bots mill around inside spawn | `ff_bot_nav_report` → `exits=0` | Spawn room is a nav island. Regenerate — generation now forces doors open |
| Bots ignore a ladder | `ff_bot_nav_report` → `ladders=0` | `.nav` predates ladder generation. Re-run `nav_generate` |
| Bots won't use an underwater tunnel | `ff_nav_visualize underwater` | `.nav` predates swim connections. Re-run `nav_generate` |
| Bots run into a wall | `bot_show_path` → red box? | Red = no path (data problem). No red = path exists but can't be followed (movement problem) |
| Bot spins in place | — | Two systems fighting for the view or for movement. Check nothing calls `Approach()` outside the arbiter |
| Snipers won't hold a position | `ff_bot_sniper_report` | If your spot has no `SNIPER_SPOT` / `AUTO_SNIPER_SPOT` / `HIGH_GROUND` tag, it was never a candidate. Author it with `ff_nav_place sniper` |
| Bots ignore your hand-placed marker | `ff_nav_validate` → marker counts | Marker has no nav area within 256u (a warning is printed at load), or the sidecar didn't load |
| Whole team does nothing useful | `ff_bot_nav_report` | No incursion data — spawn rooms untagged |
| Engineers build in silly places | `ff_nav_visualize sentry` | No sentry hints. Author with `ff_nav_place sentry` |
| Flag carriers wander instead of capping | `ff_bot_lua_report` → any `cap` rows? | Caps are `trigger_ff_script`. If none are tracked, the registry didn't see them — check `ff_bot_lua_objectives` isn't `0` |
| Bots path to objectives that aren't live | `ff_bot_lua_report` → `LIVE` vs `gone` | AvD phase gates. If a removed flag still shows `LIVE`, the `Notify_Item*` hook isn't firing |
| An entity the bots should care about is missing | `ff_bot_lua_report` | It has no `botgoaltype` and no recognised model or name. Add a model rule, or place a manual marker |
| Something is classified wrongly | `ff_bot_classify_entities 2` | Logs every inference with the model and name it matched. Entries marked `INFERRED` are guesses; `declared` ones came from Lua |
| Bots die in environmental damage | `ff_bot_hazard_response 2` | Logs every hit it classifies. Nothing logged = the damage isn't being recognised: no environmental damage bits **and** the area isn't tagged `hazardzone`. Check `ff_nav_visualize hazardzone` |
| Bots run for the suit over trivial damage | — | Exposure has to last 1s before they commit. If it's still too twitchy the `trigger_hurt` is probably larger than the actual danger |
| Whole team attacks, nobody defends | `ff_bot_gamemode_report` | Wrong mode detected, or `ff_bot_defense_fraction` is overriding it. Check the "derived from" line — no cap owners usually means the registry never saw the caps |
| Bots go to the wrong objective for the mode | `ff_bot_gamemode_report` | If the mode line is wrong, force it with `ff_bot_gamemode`. If it's right, the ladder found something higher-ranked — a keycard always outranks a flag |
| A bot refuses to go somewhere everyone else goes | `ff_bot_objective_gating 2` | It blacklisted that objective after failing to reach it. The log says which detector fired. Clears on respawn |
| Bots stand at the bottom of a lift shaft | `ff_bot_use_lifts 2` | No call button they can see within 400u, or the platform isn't recognised. `ff_nav_visualize lift` shows what got tagged; `ff_nav_place lift` covers Lua-driven platforms |
| Bots ignore a button that opens the way | — | `HandleButtons` only hunts once the bot is genuinely stuck, within 320u, with line of sight. A button behind a grate fails the LOS test |
| Defenders all stack on one spot | `ff_nav_visualize defend` | One post, or none. Posts are spread per-bot, but only across the posts that exist. If analysis ran, `ff_nav_analyze_report` says how many it derived |
| No sniper perches, overlooks or aim hints anywhere | `ff_nav_analyze_report` | "visibility data present: NO" — this `.nav` predates the analyze phase. Re-run `nav_generate` |
| Analysis derived almost nothing | `ff_nav_analyze_report` | Zero traffic means no spawn thresholds or no objective areas. Passes 3 and 4 both hang off traffic, so they produce nothing too. Check `ff_bot_nav_report` and `ff_bot_lua_report` first |
| Far too many areas tagged choke / traffic / overlook | `ff_nav_visualize cutpoint` | Thresholds are first guesses. `ff_bot_analyze_traffic_threshold` and `ff_bot_analyze_overlook_threshold` raise the bar; cut points have no threshold and are exact |
| Demomen detpack a wall that opens nothing | `ff_nav_visualize breach` | The breach test wants a detour >1200u and >1.6× the straight line. A map with a big loop around a small breakable trips it legitimately |
| Overlay vanishes after 30 seconds | — | Fixed: `ff_nav_visualize` now redraws until `ff_nav_visualize off`. If it still expires, `ff_nav_visualize_persist` is `0` |

---

## Known gaps

- **No conc-jumping or rocket-jumping.** `FF_NAV2_JUMP_SPOT` gets you a running
  duck-jump and nothing more. Self-propelled movement is a separate verb the bot
  doesn't have, and a large fraction of FF map routes assume at least conc. The
  bot takes the long way or, where there is no long way, can't reach at all.
- **Analysis is untuned.** Every threshold in `CFFBotAnalyzer` — traffic 0.35,
  overlook 0.45, breach 1.6× / 1200u, defend 2000u — is a first guess. They are
  all cvars or named constants and all want a pass over real maps with
  `ff_nav_visualize` running. The *algorithms* are exact; the cutoffs are not.
- **Nothing reads the map's Lua script.** FF ships `maps/<map>.lua` beside the
  `.bsp` and there is a Lua VM in-process. Parsing it would give the real
  prerequisite graph — which trigger enables which goal, in what order — instead
  of the keycard-rank rule and the no-progress watchdog, which are inference.
  This is the largest remaining automation win and the one most likely to hit
  surprises across 597 scripts.
- **`func_button` target strings are still unread.** The analyzer reads
  `trigger_teleport`'s destination but not which button drives which door or
  lift, so `CFFBotRideLift` still presses the nearest visible button and waits
  on a timer rather than knowing.
- **Mode detection is a heuristic over the registry.** It reads the shape of
  what's live, which is the best available signal, but a map that declares its
  goals unconventionally can be misread. `ff_bot_gamemode` forces it. The
  detection reports what it derived from so a wrong answer is diagnosable rather
  than mysterious.
- **Objective sequencing is rank plus failure, not understanding.** The keycard
  rule is a real rule; everything else is "it noticed it couldn't get there".
  That covers the general case and takes up to 25 seconds to notice the stalled
  ones, unless A* fails outright, which is immediate.
- **Lifts are timed out, not understood.** `CFFBotRideLift` waits, presses the
  nearest visible button, and gives up on a schedule. It has no model of which
  button calls which platform — deriving that needs the entity's target strings,
  which aren't reachable from here without new plumbing.
- **Removing an authored nav connection needs a map reload.** Attributes can be
  wiped and re-derived; a graph edge can't be pulled out from under a bot that's
  walking it. Deleting `linkfrom`/`linkto` markers warns and takes effect next
  load. Same constraint as `ff_bot_links_clear`.
- **`ff_nav_visualize` can't draw link markers.** They set no attribute, because
  they make an edge rather than tag an area. `ff_nav_builder_report` lists them.
