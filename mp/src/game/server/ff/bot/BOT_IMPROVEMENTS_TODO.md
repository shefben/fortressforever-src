# FF Bot — Remaining Improvements

Items not yet implemented (or partially implemented). Star ratings:
★★★ = transformative · ★★ = noticeable · ★ = polish.

Status legend: **TODO** = not started · **PARTIAL** = scaffolding / partial behavior in place.

---

## 1. Combat tactics

- **Cover-peek shooting** ★★★ — TODO. Bot walks out from corner, shoots, retreats. Currently bot sits in the open during firefights.
- **Side-step jukes** ★★ — PARTIAL. Combat strafe alternates left/right, but no random jumps/backsteps to break tracking.
- **Reload management** ★★ — TODO. Bot doesn't voluntarily reload between fights; gets caught dry-firing on first encounter after travel.
- **Wall-bang prediction** ★ — TODO. Sniper bots could fire at last-known position through thin walls.
- **Flanking** ★★★ — TODO. When 2+ teammates engage from front, bot routes around to side.
- **Pre-fire on spawn doors** ★★ — TODO. When enemy spawn door opens, pre-aim and fire.
- **Suppressive fire** ★ — TODO. Shoot at LKP to pin enemy while teammate flanks.
- **Don't all stack on one target** ★★ — TODO. Multiple bots seeing same threat → one flanks instead of redundantly aiming.
- **Trade awareness** ★ — TODO. 1HP bot doesn't peek; 1HP enemy gets pushed.
- **Ammo conservation** ★ — TODO. Sniper doesn't fire at full-HP scout from 4000u with half-charge.

## 2. Movement / mobility (FF signature jumps)

- **Conc-jumping** ★★★ — TODO. Prime conc, jump, look at sky-angle, ride blast. *The* FF skill for medics + scouts.
- **Rocket-jumping** ★★★ — TODO. Soldier fires at feet at the right pitch + jumps simultaneously. Massive verticality.
- **Pipe / sticky-jumping** ★★★ — TODO. Demo equivalent of rocket-jump.
- **Detpack-jumping** ★★ — TODO. Demo places det, jumps on top, detonates, rides blast.

## 3. Class-specific depth

### Engineer
- **Smart sentry placement scoring** ★★★ — PARTIAL. SENTRY_HINT inference exists; doesn't yet score "LOS coverage × cover behind × distance from flag" when picking among multiple hints.
- **Sentry upgrade loop** ★★ — TODO. Engineer should keep repairing+ammoing the SG to upgrade through level 1→2→3.
- **Repair-under-fire dance** ★★ — TODO. Engy strafes between SG and dispenser repairing both while dodging.
- **Move sentry / rebuild forward** ★★ — TODO. When team pushes, engy detonates SG and rebuilds further forward.
- **Spy paranoia** ★★ — TODO. Periodically turn around to check behind; verify melee-range "teammates" by class match.

### Spy
- **Backstab approach** ★★★ — TODO. When target unaware, switch to knife, circle to rear arc (>0.643 dot), close to melee, swing. Currently bot just shoots them.
- **Sap sentries** ★★★ — TODO. Spy bots should actively approach enemy sentries to sap.
- **Tranq for soft kills** ★ — TODO. Slow flag-carrier with tranq without breaking disguise.

### Sniper
- **Headshot prediction** ★★ — TODO. Aim at head height (eye position) instead of center mass for full-charge shots.
- **Counter-snipe** ★★ — TODO. When own sniper dies, next sniper bot reroutes to a different vantage.

### Medic
- **Stay-with-soldier squad** ★★★ — TODO. Pick a friendly soldier/HWGuy to follow, stay within 200u, heal them.
- **Conc for self mobility** ★★ — TODO. Medic conc-jumps to reach distant teammates fast (also part of §2).
- **Don't engage solo** ★ — TODO. Medic flees from 1v1 toward nearest teammate.

### Demoman
- **Sticky trap chokes** ★★★ — TODO. Place 4-6 stickies at choke entrance, detonate on touch. Requires pipelauncher weapon switch + multi-shot prime + IN_ATTACK2 detonation logic.
- **Pipe vertical arc adjustment** ★★★ — PARTIAL. Projectile lead time exists; pitch adjustment for arc (fire upward when target far) doesn't.
- **Pipe-bounce off walls** ★ — TODO. Calculated bouncing pipes around corners.
- **Detpack chains** ★★ — TODO. Multiple demos coordinate det timing.

### Pyro
- **Flame in spawn doorway** ★★ — TODO. Area denial — spray flames into enemy entry.
- **Combustion reflex** ★★ — TODO. Pyro is fire-resistant in FF; should panic less when on fire than other classes.

### Scout
- **Jumpgun for repositioning** ★★ — TODO. Fire jumpgun at feet for vertical / escape.

### Soldier
- **Rocket-jumping for verticality** ★★★ — TODO (also §2).
- **Splash damage focus** ★★ — TODO. Fire at floor near enemy, not directly at them; splash hits regardless of dodge.
- **Self-damage tracking** ★ — TODO. Don't RJ when low HP.

### HWGuy
- **Battlefield presence (squad with team)** ★ — TODO. HWGuy is too slow to roam alone; should hold near other teammates.

### Civilian
- **Random escape-route variation** ★★ — PARTIAL. Civilian goes to nearest hunted-escape; should vary path round-to-round.
- **Cower behind escorts** ★ — TODO. VIP positions itself behind nearest friendly when threat seen.

## 4. Squad coordination

- **Pair assignment on spawn** ★★★ — TODO. Soldier+medic, scout+escort, two engys at flag. Designated buddies stick within 300u.
- **Wave attacks** ★★ — TODO. Multiple offense bots stagger their pushes.
- **Defender role rotation** ★ — TODO. With 2+ snipers, one repositions periodically (anti-counter-snipe).
- **Teammate-aware pathing** ★★ — TODO. Don't path through teammate's sticky trap or sentry's friendly-fire zone.
- **Coordinated grenade salvos** ★★ — TODO. Two demos throw MIRVs at same choke simultaneously.
- **Body-block for VIP** ★★ — TODO. Escorts physically position between VIP and attackers.

## 5. Tactical retreat / survival

- **Fall back to teammate** ★★ — TODO. Under fire, retreat toward closest friendly rather than spawn (current behavior just goes to spawn).
- **Spawn-camping detection** ★★ — TODO. If our spawn area has 2+ enemies, exit through alternate route or stay in spawn until reinforcements.

## 6. Personality & variation

- **Difficulty tiers** ★★★ — TODO. Easy / Normal / Hard / Expert. Affects: aim accuracy, reaction time, map knowledge, weapon switch speed, grenade frequency.
- **Per-bot personality** ★★ — TODO. Aggressive / cautious / tank / trickster. Tagged at creation.
- **Reaction-time jitter** ★★ — TODO. Currently 0ms. Add 100-300ms before opening fire on a fresh sighting (varies by difficulty).
- **Aim-error model** ★★ — TODO. Gaussian aim error that narrows over hold-time (sniper) or with charge.
- **Voice-line randomization** ★ — TODO. Different bots say different things based on personality.
- **Skill ramp during round** ★ — TODO. Bot starts cold, warms to peak skill over 30s of life.

## 7. Communication

- **Voice "enemy spotted" pings** ★★ — PARTIAL. Internal team-alert system exists; no voice broadcast / HUD signal to humans.
- **"Need backup" calls** ★ — TODO. When 2v1 outnumbered.
- **"Got it" / "I'll get it"** ★ — TODO. Flag-runner signals so teammates escort instead of also running.
- **Engineer voice "spy!"** ★★ — TODO. Engineer detects nearby spy → calls out.
- **Cap announcement** ★ — TODO. Countdown when about to cap.

## 8. Anti-cheese / anti-frustration

- **Don't gang up on a single human** ★★ — TODO. When 4 bots vs 1 human, only 2 prioritize them.
- **Anti-spawn-camp** ★★ — TODO. Bots leave a spawn after killing 2 enemies there; don't camp.
- **Aim-jitter when too accurate** ★ — TODO. If bot has high accuracy streak, dial back temporarily.
- **Mercy timers** ★ — TODO. After killing same human 3× in 30s, that human gets a 5s "off-target" buffer.

## 9. Persistence & stats

- **Per-bot session stats** ★ — TODO. Kills/deaths/captures per bot.
- **Bot leaderboard** ★ — TODO. End-of-round display.
- **Persistent bot identity** ★ — TODO. Same name+personality across rounds (until server restart).
- **Per-server config** ★ — TODO. `cfg/bots/2fort.cfg` for map-specific bot counts.

## 10. Dev / debug / config

- **`bot_difficulty <0-3>`** ★★ — TODO (depends on difficulty tier work in §6).
- **`bot_quota_mode <fill|match>`** ★★ — TODO. Fill empty slots vs match human count.
- **`bot_freeze_all` / `bot_unfreeze_all`** ★ — TODO. Testing helpers.
- **`bot_show_path`** ★ — TODO. Render path lines for live bots.
- **`bot_show_threat`** ★ — TODO. Render line to current threat.
- **`bot_force_class <class>`** ★ — TODO. For testing.
- **Profile command** ★ — TODO. Measure bot CPU per tick.
- **Replay system** ★★ — TODO. Record bot decisions for postmortem.

## 11. Quality-of-life for humans

- **Bot-host friendliness** ★ — TODO. When bot kills a human, voice line is non-toxic.
- **Match-difficulty floor** ★ — TODO. If humans losing badly, bots reduce skill (depends on difficulty tier work).
- **Bot side-trash** ★ — TODO. Bots make banter / taunts; gives a human server life.

## 12. Map intelligence (extras)

- **Auto-generation on first map load** ★★ — TODO. If `.nav` is missing on listen-server, kick off `nav_generate` automatically rather than refusing to spawn bots.
- **`ff_nav_generate_all`** ★ — TODO. Walks `cfg/maplist.txt` and bulk-generates.
- **Mapper hint entities (`info_ff_bot_hint` etc.)** — DELIBERATELY SKIPPED per scope ("must work with already-created maps"). Not in this list.

## 13. Squad-level intelligence (deeper)

- **Route entropy: persistent route-memory population** — PARTIAL. `m_lastRouteChokeID` field is populated when bot crosses a choke, but the per-bot path bias only nudges away from one choke; could track multiple recent chokes.
- **Heatmap-driven hint promotion** — PARTIAL. Hot zones drive pre-aim and danger penalty, but no semantic promotion to a `DANGER_ZONE`-style tag for downstream consumers.

---

## Priority recommendations (when picking next batches)

1. **Difficulty tiers + reaction-time + aim-error model** ★★★ — biggest single change for human enjoyment.
2. **Squad pairing (medic-on-soldier)** ★★★ — biggest "AI feel" upgrade.
3. **Cover-peek shooting** ★★★ — instantly makes bots look smart.
4. **Spy backstab approach + sap sentries** ★★★ — most class-defining FF gameplay.
5. **Smart sentry-placement scoring** ★★★ — fixes "engineer built SG in a corner with no LOS" problem.
6. **Conc-jumping + rocket-jumping** ★★★ — FF-signature movement that makes bots feel like real FF players.
7. **Demoman sticky-trap chokes** ★★★ — true demo gameplay.
8. **Pipe vertical-arc pitch** ★★★ — makes demo bots actually hit things at range.
9. **Auto-generation on first map load** ★★ — fixes "map missing nav" frustration.
10. **`bot_show_path` + `bot_show_threat` debug overlays** ★★ — fast iteration tools for the above.
