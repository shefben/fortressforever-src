//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "ff_gamestate.h"

// Local bot utility headers
#include "bot_constants.h"
#include "bot_profile.h"
#include "bot_util.h"       // For UTIL_ConstructBotNetName

#include "engine/iserverplugin.h"
#include "icommandline.h"

// TODO: Determine if "cs_shareddefs.h" has an FF equivalent or if its contents are covered by other headers like bot_constants.h or ff_shareddefs.h
// #include "cs_shareddefs.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#pragma warning( disable : 4355 )			// warning 'this' used in base member initializer list - we're using it safely


//--------------------------------------------------------------------------------------------------------------
static void PrefixChanged( IConVar *c, const char *oldPrefix, float flOldValue )
{
	if ( TheFFBots() && TheFFBots()->IsServerActive() )
	{
		for( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *player = static_cast<CBasePlayer *>( UTIL_PlayerByIndex( i ) );

			if ( !player )
				continue;

			if ( !player->IsBot() || !player->IsEFlagSet(FL_CLIENT) )
				continue;

			CFFBot *bot = dynamic_cast< CFFBot * >( player );

			if ( !bot || !bot->GetProfile() ) // Added null check for GetProfile()
				continue;

			// set the bot's name
			char botName[MAX_PLAYER_NAME_LENGTH]; // MAX_PLAYER_NAME_LENGTH from bot_constants.h or similar
			// TODO_FF: Ensure UTIL_ConstructBotNetName is available and working with FF BotProfile
			// For now, assuming it's in bot_util.h or will be.
			// UTIL_ConstructBotNetName( botName, MAX_PLAYER_NAME_LENGTH, bot->GetProfile() );
			Q_snprintf( botName, sizeof(botName), "%s", bot->GetProfile()->GetName()); // Simplified fallback


			if (engine)
				engine->SetFakeClientConVarValue( bot->edict(), "name", botName );
		}
	}
}


ConVar cv_bot_traceview( "bot_traceview", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_stop( "bot_stop", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, immediately stops all bot processing." );
ConVar cv_bot_show_nav( "bot_show_nav", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_walk( "bot_walk", "0", FCVAR_REPLICATED, "If nonzero, bots can only walk, not run." );
ConVar cv_bot_difficulty( "bot_difficulty", "1", FCVAR_REPLICATED, "Defines the skill of bots joining the game.  Values are: 0=easy, 1=normal, 2=hard, 3=expert." );
ConVar cv_bot_debug( "bot_debug", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_debug_target( "bot_debug_target", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "For internal testing purposes." );
ConVar cv_bot_quota( "bot_quota", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Determines the total number of bots in the game." );
ConVar cv_bot_quota_mode( "bot_quota_mode", "normal", FCVAR_REPLICATED, "Determines the type of quota.\nAllowed values: 'normal', 'fill', and 'match'.\nIf 'fill', the server will adjust bots to keep N players in the game, where N is bot_quota.\nIf 'match', the server will maintain a 1:N ratio of humans to bots, where N is bot_quota." );
ConVar cv_bot_prefix( "bot_prefix", "", FCVAR_REPLICATED, "This string is prefixed to the name of all bots that join the game.\n<difficulty> will be replaced with the bot's difficulty.\n<weaponclass> will be replaced with the bot's desired weapon class.\n<skill> will be replaced with a 0-100 representation of the bot's skill.", PrefixChanged );
ConVar cv_bot_allow_rogues( "bot_allow_rogues", "1", FCVAR_REPLICATED, "If nonzero, bots may occasionally go 'rogue'. Rogue bots do not obey radio commands, nor pursue scenario goals." );
ConVar cv_bot_allow_pistols( "bot_allow_pistols", "1", FCVAR_REPLICATED, "If nonzero, bots may use pistols." );
ConVar cv_bot_allow_shotguns( "bot_allow_shotguns", "1", FCVAR_REPLICATED, "If nonzero, bots may use shotguns." );
ConVar cv_bot_allow_sub_machine_guns( "bot_allow_sub_machine_guns", "1", FCVAR_REPLICATED, "If nonzero, bots may use sub-machine guns." );
ConVar cv_bot_allow_rifles( "bot_allow_rifles", "1", FCVAR_REPLICATED, "If nonzero, bots may use rifles." );
ConVar cv_bot_allow_machine_guns( "bot_allow_machine_guns", "1", FCVAR_REPLICATED, "If nonzero, bots may use the machine gun." );
ConVar cv_bot_allow_grenades( "bot_allow_grenades", "1", FCVAR_REPLICATED, "If nonzero, bots may use grenades." );
ConVar cv_bot_allow_snipers( "bot_allow_snipers", "1", FCVAR_REPLICATED, "If nonzero, bots may use sniper rifles." );
// TODO: Update for FF if shields are different or not present
// #ifdef CS_SHIELD_ENABLED // This was CS specific
// ConVar cv_bot_allow_shield( "bot_allow_shield", "1", FCVAR_REPLICATED );
// #endif // CS_SHIELD_ENABLED
ConVar cv_bot_join_team( "bot_join_team", "any", FCVAR_REPLICATED, "Determines the team bots will join into. Allowed values: 'any', 'T', or 'CT'." ); // TODO: Update T/CT for FF teams
ConVar cv_bot_join_after_player( "bot_join_after_player", "1", FCVAR_REPLICATED, "If nonzero, bots wait until a player joins before entering the game." );
ConVar cv_bot_auto_vacate( "bot_auto_vacate", "1", FCVAR_REPLICATED, "If nonzero, bots will automatically leave to make room for human players." );
ConVar cv_bot_zombie( "bot_zombie", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, bots will stay in idle mode and not attack." );
ConVar cv_bot_defer_to_human( "bot_defer_to_human", "0", FCVAR_REPLICATED, "If nonzero and there is a human on the team, the bots will not do the scenario tasks." );
ConVar cv_bot_chatter( "bot_chatter", "normal", FCVAR_REPLICATED, "Control how bots talk. Allowed values: 'off', 'radio', 'minimal', or 'normal'." );
ConVar cv_bot_profile_db( "bot_profile_db", "BotProfile.db", FCVAR_REPLICATED, "The filename from which bot profiles will be read." ); // TODO: Ensure BotProfile.db is correct for FF
ConVar cv_bot_dont_shoot( "bot_dont_shoot", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, bots will not fire weapons (for debugging)." );
ConVar cv_bot_eco_limit( "bot_eco_limit", "2000", FCVAR_REPLICATED, "If nonzero, bots will not buy if their money falls below this amount." );
ConVar cv_bot_auto_follow( "bot_auto_follow", "0", FCVAR_REPLICATED, "If nonzero, bots with high co-op may automatically follow a nearby human player." );
ConVar cv_bot_flipout( "bot_flipout", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "If nonzero, bots use no CPU for AI. Instead, they run around randomly." );


// extern void FinishClientPutInServer( CFFPlayer *pPlayer ); // This is usually in player.h or similar


//--------------------------------------------------------------------------------------------------------------
// Engine callback for custom server commands
void Bot_ServerCommand( void )
{
}

// CFFBot constructor, destructor, Initialize, ResetValues, Spawn are in ff_bot.cpp
// This file (ff_bot_init.cpp) only contains ConVar setup and related hooks like PrefixChanged.
/*
// ... (Removed CFFBot method definitions as they are in ff_bot.cpp) ...
*/
