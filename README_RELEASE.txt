================================================================================
  Fortress Forever — Bot-Capable server.dll
================================================================================

This release contains a custom-built server.dll that ports Valve's NextBot /
TFBot intelligence into Fortress Forever. The bots are class-aware: spies
backstab and sap, engineers build sentries, medics follow front-liners,
snipers hold elevated positions, demos lay sticky traps, etc.

--------------------------------------------------------------------------------
  Installation
--------------------------------------------------------------------------------

1. Find your Fortress Forever install. Default Steam path:
     ...\Steam\steamapps\common\Fortress Forever\sdk2013\fortressforever\

2. Back up the existing bin\server.dll (and bin\server.pdb if present) so you
   can revert if needed.

3. Copy server.dll (and optionally server.pdb) from this zip's bin\ folder
   into:
     ...\Fortress Forever\sdk2013\fortressforever\bin\

4. Launch Fortress Forever as you normally would.

--------------------------------------------------------------------------------
  Generating navigation data (one-time per map)
--------------------------------------------------------------------------------

Bots need a per-map .nav file describing where they can walk. If the file is
missing, the bot manager will refuse to spawn bots on that map. You only have
to do this once per map; the .nav file is saved to disk and loaded
automatically on every future map load.

Steps:

  1. Start the server on the map you want to play, e.g.
        map ff_2fort

  2. Open the developer console (~) and enable cheats:
        sv_cheats 1

  3. Generate the navigation mesh (this can take 30 seconds to several
     minutes depending on map size; the screen will appear frozen — leave it
     alone until it finishes):
        nav_generate

     The map will reload when generation completes. The .nav file is saved
     automatically to:
        ...\fortressforever\maps\<mapname>.nav

That's the only data file required. There is no separate "ffdata" file —
this build dropped the old .ffnav sidecar and now writes everything to the
standard .nav format.

If you ever change a map's geometry (or want to redo coverage), repeat the
steps above; the new .nav will overwrite the old one.


  Verifying the nav covers gameplay entities

After generating, run:
        ff_nav_validate

This walks every spawn, flag, cap, and resupply entity and warns you if any
of them are too far from the nav mesh for the bots to navigate to. Common
reasons a flag or cap won't be reached: the entity is set inside a wall or
floor, or you generated nav while standing on top of a non-walkable surface.

You can also visualize the bot's view of the map with:
        ff_nav_visualize spawn|exit|flag|cap|resupply|sniper|sentry|combat|all

This draws colored boxes over each tagged area type for ~10 seconds.


  Optional: hand-tagged sniper / sentry spots

The bots auto-discover elevated sniper perches near each cap point, but you
can hand-tag specific areas if you want stronger control. Use the standard
Source nav-edit workflow:

        nav_edit 1
        # walk to the area you want, look at it, then:
        mark
        nav_attribute_set FF_NAV_SNIPER_SPOT       # for sniper perches
        nav_attribute_set FF_NAV_SENTRY_SPOT       # for engineer build sites
        nav_attribute_set FF_NAV_HUNTED_ESCAPE     # Hunted-mode VIP goal
        nav_save

Tags survive across map loads (they're written to the .nav file).

--------------------------------------------------------------------------------
  Adding and managing bots
--------------------------------------------------------------------------------

All bot commands require sv_cheats 1 (FF's bot system is gated on cheats).

  bot_add [-count N] [-team blue|red|yellow|green|random] [-class CLASSNAME]
          [-name NAME] [-frozen] [-all]

      Spawn a bot. Without flags, picks a random class on the smallest team.
      Examples:
        bot_add                              # one random bot
        bot_add -count 8                     # eight random bots
        bot_add -team blue -class engineer   # specific role
        bot_add -all                         # one of each class

      -frozen makes the bot stand still after spawning (useful for testing).

      Class names: scout, sniper, soldier, demoman, medic, hwguy, pyro,
      spy, engineer, civilian.

  bot_kick [N]              Remove N bots (default 1).

  bot_changeclass <class>   Force every bot to switch to the given class on
                            their next respawn.

  bot_diagnose [name]       Print the named bot's current state (action
                            stack, target, ammo, last threat). With no name,
                            prints state for all bots.

  bot_teleport <name> <x> <y> <z> <pitch> <yaw> <roll>
                            Force a bot to a specific spot. Useful for
                            testing route logic.

  bot_status                Prints health/armor for each bot to console.

  bot_flashlight            Toggle bots' flashlights.

The bot manager auto-balances teams every 30 seconds: if the gap between
the largest and smallest team exceeds 1, a bot is moved from the bigger
team to the smaller. Carrier bots are skipped for the move.

--------------------------------------------------------------------------------
  Tuning convars (sv_cheats 1)
--------------------------------------------------------------------------------

These are safe to tweak live; defaults are sane.

  ff_bot_ammo_search_range          5000   Travel-distance limit for ammo runs
  ff_bot_health_search_range        2000   Travel-distance limit for health runs
  ff_bot_retreat_to_cover_range     1000   How far to look for a cover spot
  ff_bot_wait_in_cover_min_time     1      Min seconds bot rests in cover
  ff_bot_wait_in_cover_max_time     2      Max seconds bot rests in cover
  ff_bot_melee_attack_abandon_range 500    Drop melee chase if target this far
  ff_bot_spy_knife_range            300    Knife distance for spy bots
  ff_bot_spy_change_target_range_threshold
                                    300    Range delta to swap victims
  ff_bot_sniper_patience_duration   10     Seconds idle before relocating
  ff_bot_sniper_melee_range         300    Sniper switches to melee inside this
  ff_nav_combat_build_rate          0.05   Per-shot combat-intensity gain
  ff_nav_combat_decay_rate          0.022  Per-second decay back toward 0

--------------------------------------------------------------------------------
  Troubleshooting
--------------------------------------------------------------------------------

* "Bots won't spawn" — make sure sv_cheats is 1 and the map has a .nav file.
  Check the console after `bot_add` for messages from [CFFBot].

* "Bot just stands in spawn" — usually means there's no path between the
  spawn area and any objective. Run `ff_nav_validate` and look for warnings
  about uncovered spawns or flags. If a flag is uncovered, you may need to
  edit the nav near it (`nav_edit 1`, walk over the flag, `nav_mark_walkable`,
  `nav_generate`). For snipers specifically, the bot will fall back to a
  cap-area position if no FF_NAV_SNIPER_SPOT is hand-tagged on the map.

* "Bot picks weird routes" — that's intentional; each bot has a route flavor
  (dry vs water vs neutral) chosen at spawn so different bots take different
  corridors. Reload the map to reroll.

* "Wrong server.dll loaded" — verify the file timestamp and size match the
  one in this zip. Some installs have multiple bin\ folders; the one used
  is the one inside `sdk2013\fortressforever\bin\`.

* Reverting — replace bin\server.dll with the backup you took before
  installing.

================================================================================
