//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: ConVars for Fortress Forever Bots
//
//=============================================================================//

#include "cbase.h"
#include "ff_bot_manager.h" // For BotDifficultyType and potentially other shared enums/structs

// Bot Behavior
ConVar bot_difficulty( "bot_difficulty", "1", FCVAR_NONE, "Defines the skill of bots joining the game. Values are: 0=easy, 1=normal, 2=hard, 3=expert." );
ConVar bot_quota( "bot_quota", "0", FCVAR_NONE, "Determines the total number of bots in the game." );
ConVar bot_quota_mode( "bot_quota_mode", "normal", FCVAR_NONE, "Determines the type of quota. Allowed values: 'normal', 'fill', and 'match'. If 'fill', the server will adjust bots to keep N players in the game, where N is bot_quota. If 'match', the server will maintain a 1:N ratio of humans to bots, where N is bot_quota." );
ConVar bot_join_team( "bot_join_team", "any", FCVAR_NONE, "Determines the team bots will join. Allowed values: 'any', 'red', 'blue'." ); // Add 'yellow', 'green' if applicable
ConVar bot_join_after_player( "bot_join_after_player", "1", FCVAR_NONE, "If nonzero, bots wait to join until a human player joins." );
ConVar bot_auto_vacate( "bot_auto_vacate", "1", FCVAR_NONE, "If nonzero, bots will automatically leave to make room for human players." );
ConVar bot_chatter( "bot_chatter", "normal", FCVAR_NONE, "Control how bots talk. Allowed values: 'off', 'radio', 'minimal', 'normal'." ); // Ensure BotChatterType enum exists and matches
ConVar bot_prefix( "bot_prefix", "", FCVAR_NONE, "Prefix for bot names. Max 31 characters." ); // From CS
ConVar bot_defer_to_human( "bot_defer_to_human", "0", FCVAR_NONE, "If nonzero, bots will prefer to let humans complete objectives." ); // From CS, might be useful for FF

// Bot Weapon Restrictions (FF Specific Categories)
// These mirror CS style "allow" ConVars.
// Set default to 1 (allowed) for most common/expected weapons.
ConVar ff_bot_allow_pistols( "ff_bot_allow_pistols", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use pistols." );
ConVar ff_bot_allow_shotguns( "ff_bot_allow_shotguns", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use shotguns." );
ConVar ff_bot_allow_sub_machine_guns( "ff_bot_allow_sub_machine_guns", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use sub-machine guns." );
ConVar ff_bot_allow_rifles( "ff_bot_allow_rifles", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use rifles (e.g., Sniper Rifle if not covered by sniper specific). General purpose category." );
ConVar ff_bot_allow_machine_guns( "ff_bot_allow_machine_guns", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use machine guns (e.g., HWGuy Minigun if not covered by specific). General purpose category." );
ConVar ff_bot_allow_grenades( "ff_bot_allow_grenades", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use grenades (frag, conc, emp, nail etc)." );
ConVar ff_bot_allow_rocket_launchers( "ff_bot_allow_rocket_launchers", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use rocket launchers." );
ConVar ff_bot_allow_flamethrowers( "ff_bot_allow_flamethrowers", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use flamethrowers." );
ConVar ff_bot_allow_pipe_launchers( "ff_bot_allow_pipe_launchers", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use pipebomb launchers." );
ConVar ff_bot_allow_miniguns( "ff_bot_allow_miniguns", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use miniguns (HWGuy primary)." );
ConVar ff_bot_allow_sniper_rifles( "ff_bot_allow_sniper_rifles", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use sniper rifles." );
ConVar ff_bot_allow_mediguns( "ff_bot_allow_mediguns", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use mediguns/healing tools." ); // Utility
ConVar ff_bot_allow_tranqguns( "ff_bot_allow_tranqguns", "1", FCVAR_GAMEDLL, "If nonzero, bots are allowed to use tranquilizer guns (Spy primary)." ); // Utility/Special
// Add ConVars for any other major weapon categories specific to FF classes, e.g., special spy weapons, engineer weapons if they have unique bot logic.

// Debugging
ConVar bot_debug( "bot_debug", "0", FCVAR_GAMEDLL | FCVAR_CHEAT, "Bot debugging visualizations." );
ConVar bot_debug_target( "bot_debug_target", "", FCVAR_GAMEDLL | FCVAR_CHEAT, "Only debug the bot with this name." );

// This needs to be included for the ConVars to be registered.
// It's often done in a central cpp file for the project, but for bots, it can be here.
// If there's another file that already does this for other bot cvars, add these there instead.
// For now, let's assume this file is responsible for its ConVars.
static ConCommandBase *s_pBotCvars[] =
{
	&bot_difficulty,
	&bot_quota,
	&bot_quota_mode,
	&bot_join_team,
	&bot_join_after_player,
	&bot_auto_vacate,
	&bot_chatter,
	&bot_prefix,
	&bot_defer_to_human,
	&ff_bot_allow_pistols,
	&ff_bot_allow_shotguns,
	&ff_bot_allow_sub_machine_guns,
	&ff_bot_allow_rifles,
	&ff_bot_allow_machine_guns,
	&ff_bot_allow_grenades,
	&ff_bot_allow_rocket_launchers,
	&ff_bot_allow_flamethrowers,
	&ff_bot_allow_pipe_launchers,
	&ff_bot_allow_miniguns,
	&ff_bot_allow_sniper_rifles,
	&ff_bot_allow_mediguns,
	&ff_bot_allow_tranqguns,
	&bot_debug,
	&bot_debug_target,
	// Add other ConVars here from above
	NULL // Terminator
};

// Helper to register all ConVars (call this from somewhere appropriate, e.g. ModSpecificInitialize)
// For now, this function might not be automatically called. This is more of a declaration.
// In many Source mods, ConVar registration is automatic if the file is compiled.
// If not, a call to a function like this from a known engine callback is needed.
void RegisterFFBotCVars( void )
{
	// This function is illustrative. Actual registration might be handled by just compiling this file.
	// If explicit registration is needed, this would be the place, or iterate s_pBotCvars.
	Msg( "Registering Fortress Forever Bot CVars\n" );
}

// Some ConVars from CS that are used by ff_bot_manager.cpp that might need to be declared here if not already global:
// ConVar cv_bot_difficulty( "bot_difficulty", "1", 0, "Defines the skill of bots joining the game. Values are: 0=easy, 1=normal, 2=hard, 3=expert." );
// ConVar cv_bot_quota( "bot_quota", "0" );
// ConVar cv_bot_quota_mode( "bot_quota_mode", "normal" );
// ConVar cv_bot_join_team( "bot_join_team", "any" );
// ConVar cv_bot_join_after_player( "bot_join_after_player", "1" );
// ConVar cv_bot_auto_vacate( "bot_auto_vacate", "1" );
// ConVar cv_bot_chatter( "bot_chatter", "normal" );
// ConVar cv_bot_prefix( "bot_prefix", "" );
// ConVar cv_bot_defer_to_human( "bot_defer_to_human", "0" );
// ConVar cv_bot_debug( "bot_debug", "0", FCVAR_CHEAT );

// Note: friendlyfire is extern ConVar in ff_bot_manager.h, usually a game-wide cvar.
// mp_freezetime (used in cs_bot_manager.cpp for round start) would be FFGameRules()->GetFreezeTime() in FF.
// mp_autoteambalance, mp_limitteams are also game-wide cvars.
// nav_edit is a global engine cvar.

// The ConVars like ff_bot_allow_pistols were previously declared as extern in ff_bot_manager.cpp.
// Now they are defined here. The extern declarations in ff_bot_manager.cpp should be removed or this file included.
// For simplicity, we'll assume this file being compiled is enough for now, and remove externs from the .cpp.

[end of mp/src/game/server/ff/bot/ff_bot_cvars.cpp]
