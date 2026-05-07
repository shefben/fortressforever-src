//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Basic BOT handling.
//
// $Workfile:     $
// $Date: 2005/09/29 19:26:37 $
//
//-----------------------------------------------------------------------------
// $Log: ff_bot_temp.cpp,v $
// Revision 1.11  2005/09/29 19:26:37  mulchman
// no message
//
// Revision 1.10  2005/09/29 19:21:52  mulchman
// no message
//
// Revision 1.9  2005/09/29 19:19:10  mulchman
// no message
//
// Revision 1.8  2005/09/28 19:06:06  mulchman
// no message
//
// Revision 1.7  2005/09/21 14:35:37  mulchman
// no message
//
// Revision 1.6  2005/09/18 18:10:48  mulchman
// no message
//
// Revision 1.5  2005/08/26 22:00:12  fryguy
// fix bot bug added with new class menus and such
//
// Revision 1.4  2005/08/03 06:38:49  fryguy
// remove icon from hud_message (I'm not understanding why it's there)
// fix a bug with the status icons not overwriting previous icons
// buttons in maps without lua files should be usable now
// buttons can no longer be used over and over again when they are at their peak
// bots now actively change classes when instructed to with bot_changeclass <number>
// lua: BroadcastSound now works, and added BroadcastMessage and RespawnAllPlayers (still has a couple bugs)
// trigger_multiple entities now override the OnTrigger method when used in maps if instructed by lua (to create respawn doors)
// Add engineer regenning armor (TODO: fix clamping)
// Remove class for player when they change team (so they don't immediately spawn when choosing a new team)
// Ragdolls are now removed after 30 seconds of being in game
// Remove the really annoying screen fade thingy when you die because of fall damage. BEEP BEEP BEEEEEP
// Change concs again slightly
//
// Revision 1.3  2005/07/26 01:21:47  mulchman
// no message
//
// Revision 1.2  2005/06/27 16:32:38  mulchman
// no message
//
// Revision 1.1  2005/02/20 21:54:21  billdoor
// no message
//
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "player.h"
#include "ff_player.h"
#include "in_buttons.h"
#include "movehelper_server.h"
#include "gameinterface.h"
#include "ff_utils.h"
#include "ff_team.h"
#include "te_effect_dispatch.h"
#include "bitbuf.h"
#include "filesystem.h"

// TODO: REMOVE ME REMOVE ME
//#include "ff_detpack.h"

#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"

// CFFBot is now in ff/bot/ff_bot.{cpp,h} (NextBot-derived). The legacy
// Bot_Think / Bot_GhostThinkRecord / Bot_GhostThinkPlayback forwards have
// been removed along with the random-walk implementation that used them.

ConVar bot_forcefireweapon( "bot_forcefireweapon", "", 0, "Force bots with the specified weapon to fire." );
ConVar bot_forceattack2( "bot_forceattack2", "0", 0, "When firing, use attack2." );
ConVar bot_forceattackon( "bot_forceattackon", "0", 0, "When firing, don't tap fire, hold it down." );
ConVar bot_flipout( "bot_flipout", "0", 0, "When on, all bots fire their guns." );
ConVar bot_changeclass( "bot_changeclass", "0", 0, "Force all bots to change to the specified class." );
static ConVar bot_mimic( "bot_mimic", "0", 0, "Bot uses usercmd of player by index." );
static ConVar bot_mimic_yaw_offset( "bot_mimic_yaw_offset", "0", 0, "Offsets the bot yaw." );

ConVar bot_sendcmd( "bot_sendcmd", "", 0, "Forces bots to send the specified command." );

ConVar bot_crouch( "bot_crouch", "0", 0, "Bot crouches" );

//////////////////////////////////////////////////////////////////////////

ConVar bot_ghostrecord( "bot_ghostrecord", "-1", 0, "Record a clients usercommands to file." );
ConVar bot_ghostplayback( "bot_ghostplayback", "-1", 0, "Playback a clients usercommands from file." );

//////////////////////////////////////////////////////////////////////////

static int g_CurBotNumber = 1;

CON_COMMAND_F(bot_immuneme, "Temporary immunity", FCVAR_CHEAT)
{
	CFFPlayer *pHuman = ToFFPlayer( UTIL_GetCommandClient() );
	if( pHuman )
	{
		pHuman->Cure( NULL );
	}
}

CON_COMMAND(bot_cloak, "cloak!")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_SpyCloak();
		}
	}
}

CON_COMMAND( bot_slot, "change to slot x" )
{
	int iSlot = 1;

	const char* pszString = args[1];
	if( !pszString )
		return;

	switch( pszString[0] )
	{
		case '1': iSlot = 1; break;
		case '2': iSlot = 2; break;
		case '3': iSlot = 3; break;
		case '4': iSlot = 4; break;
		case '5': iSlot = 5; break;
		default: Warning( "[bot_slot] Default to slot 1\n" ); break;
	}

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Weapon_Switch( pPlayer->Weapon_GetSlot( iSlot ) );
		}
	}
}

CON_COMMAND(bot_scloak, "scloak!")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_SpySilentCloak();
		}
	}
}

CON_COMMAND( bot_dropitems, "drop items" )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CFFPlayer *pPlayer = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if( pPlayer && ( pPlayer->GetFlags() & FL_FAKECLIENT ) )
		{
			pPlayer->Command_DropItems();
		}
	}
}

CON_COMMAND( bot_savesentry, "makes a bot save his sentry" )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CFFPlayer *pPlayer = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if( pPlayer && pPlayer->GetSentryGun() )
		{
			CFFSentryGun *pSentryGun = pPlayer->GetSentryGun();
			pSentryGun->Upgrade();
			pSentryGun->Repair(200);
			pSentryGun->AddAmmo(200, 200);
		}
	}
}

CON_COMMAND(bot_buildsentry, "build an sg")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_BuildSentryGun();
		}
	}

}

CON_COMMAND(bot_builddispenser, "build a dispenser")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_BuildDispenser();
		}
	}

}

CON_COMMAND(bot_buildsg, "build an sg")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_BuildSentryGun();
		}
	}

}

CON_COMMAND(bot_disguise, "trigger a disguise")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Bot_Disguise(TEAM_RED, CLASS_SOLDIER);
		}
	}
}

CON_COMMAND(bot_disguisez, "trigger a disguise")
{
	if ((!args[1] || !args[1][0]) || (!args[2] || !args[2][0]))
		return;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			int iTeam = TEAM_UNASSIGNED;
			int iClass = CLASS_NONE;

			if (!Q_stricmp( args[1], "blue"))
				iTeam = TEAM_BLUE;
			else if( !Q_stricmp( args[1], "red" ) )
				iTeam = TEAM_RED;
			else if( !Q_stricmp( args[1], "yellow" ) )
				iTeam = TEAM_YELLOW;
			else if( !Q_stricmp( args[1], "green" ) )
				iTeam = TEAM_GREEN;

			if( !Q_stricmp( args[2], "scout" ) )
				iClass = CLASS_SCOUT;
			else if( !Q_stricmp( args[2], "sniper" ) )
				iClass = CLASS_SNIPER;
			else if( !Q_stricmp( args[2], "soldier" ) )
				iClass = CLASS_SOLDIER;
			else if( !Q_stricmp( args[2], "demoman" ) )
				iClass = CLASS_DEMOMAN;
			else if( !Q_stricmp( args[2], "medic" ) )
				iClass = CLASS_MEDIC;
			else if( !Q_stricmp( args[2], "hwguy" ) )
				iClass = CLASS_HWGUY;
			else if( !Q_stricmp( args[2], "pyro" ) )
				iClass = CLASS_PYRO;
			else if( !Q_stricmp( args[2], "spy" ) )
				iClass = CLASS_SPY;
			else if( !Q_stricmp( args[2], "engineer" ) )
				iClass = CLASS_ENGINEER;

			if( iTeam != TEAM_UNASSIGNED && iClass != CLASS_NONE )
			{
				Warning( "[Bot %s] Disguising as: %s %s\n", pPlayer->GetPlayerName(), args[1], args[2] );
				pPlayer->Bot_Disguise( iTeam, iClass );
			}			
		}
	}
}

CON_COMMAND( bot_dmggun, "Makes a bot attack your sg" )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CFFPlayer *pPlayer = ToFFPlayer( UTIL_PlayerByIndex( i ) );

		if( pPlayer && ( pPlayer->GetFlags() & FL_FAKECLIENT ) )
		{
			CFFPlayer *pOwner = ToFFPlayer( UTIL_GetCommandClient() );
			if( pOwner )
			{
				if( pOwner->GetSentryGun() )
				{
					Warning( "[Bot %s] Sending damage to %s's sentrygun!\n", pPlayer->GetPlayerName(), pOwner->GetPlayerName() );
					( pOwner->GetSentryGun() )->TakeDamage( CTakeDamageInfo( pPlayer, pPlayer, 999999.0f, DMG_DIRECT ) );
				}
			}
		}
	}
}

CON_COMMAND( bot_showhealth, "Makes a bot show his health" )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CFFPlayer *pPlayer = ToFFPlayer( UTIL_PlayerByIndex( i ) );

		if( pPlayer && ( pPlayer->GetFlags() & FL_FAKECLIENT ) )
		{
			Warning( "[Bot %s] Health: %i (%i%%), Armor: %i (%i%%), Alive: %s: Lifestate: %i\n", pPlayer->GetPlayerName(), pPlayer->GetHealth(), pPlayer->GetHealthPercentage(), pPlayer->GetArmor(), pPlayer->GetArmorPercentage(), pPlayer->IsAlive() ? "Yes" : "No", (int) pPlayer->m_lifeState );
		}
	}
}

CON_COMMAND(bot_flashlight, "Toggle bots' flashlights")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
			pPlayer->FlashlightIsOn() ? pPlayer->FlashlightTurnOn() : pPlayer->FlashlightTurnOff();
	}
}

CON_COMMAND(bot_saveme, "have a bot do saveme")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_SaveMe();
		}
	}
}

CON_COMMAND(bot_engyme, "have a bot do engyme")
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT))
		{
			pPlayer->Command_EngyMe();
		}
	}
}

CON_COMMAND( bot_status, "Make bot show health / armor" )
{
	for( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CFFPlayer *pPlayer = ToFFPlayer( UTIL_PlayerByIndex( i ) );
		if( pPlayer && ( ( pPlayer->GetFlags() & FL_FAKECLIENT) || pPlayer->IsBot() ) )
		{
			Warning( "[Bot %s] Health: %i (%i%%) Armor: %i (%i%%)\n", pPlayer->GetPlayerName(), pPlayer->GetHealth(), pPlayer->GetHealthPercentage(), pPlayer->GetArmor(), pPlayer->GetArmorPercentage() );
		}
	}
}

CON_COMMAND_F(ffdev_gibs, "Create gibbing effect", FCVAR_CHEAT)
{
	CFFPlayer *you = ToFFPlayer(UTIL_GetCommandClient());

	CEffectData effect;
	effect.m_nEntIndex = you->entindex();

	DispatchEffect("Gib", effect);
}

CON_COMMAND_F(ffdev_legshotme, "Legshot effect test", FCVAR_CHEAT)
{
	CFFPlayer *you = ToFFPlayer(UTIL_GetCommandClient());
	you->AddSpeedEffect(SE_LEGSHOT, 999, 0.5f, SEM_ACCUMULATIVE|SEM_HEALABLE, FF_STATUSICON_LEGINJURY, 15.0f);
}

CON_COMMAND_F(ffdev_tranqme, "Tranq effect test", FCVAR_CHEAT)
{
	CFFPlayer* you = ToFFPlayer(UTIL_GetCommandClient());
	you->AddSpeedEffect(SE_TRANQ, 6.0, 0.3f, SEM_BOOLEAN | SEM_HEALABLE, FF_STATUSICON_TRANQUILIZED, 6.0f);

	CSingleUserRecipientFilter user(you);
	user.MakeReliable();

	UserMessageBegin(user, "FFViewEffect");
	WRITE_BYTE(FF_VIEWEFFECT_TRANQUILIZED);
	WRITE_FLOAT(6.0f);
	MessageEnd();
}

CON_COMMAND_F(ffdev_gasvieweffectme, "Gas view effect test", FCVAR_CHEAT)
{
	CFFPlayer *you = ToFFPlayer(UTIL_GetCommandClient());

	CSingleUserRecipientFilter user(you);
	user.MakeReliable();

	UserMessageBegin(user, "FFViewEffect");
	WRITE_BYTE(FF_VIEWEFFECT_GASSED);
	WRITE_FLOAT(6.0f);
	MessageEnd();
}

CON_COMMAND_F(ffdev_score, "Add points to, or deduct from, your current team", FCVAR_CHEAT)
{
	if (!args[1] || args[1] == 0)
		return;

	CFFPlayer* you = ToFFPlayer(UTIL_GetCommandClient());
	you->AddPointsToTeam(10, true);
	you->AddPointsToTeam(atoi(args[1]), true);
}

CON_COMMAND_F(ffdev_conc, "Concussion effect test. (Default is infinite; argument determines length of effect.)", FCVAR_CHEAT)
{
	CFFPlayer* you = ToFFPlayer(UTIL_GetCommandClient());

	you->m_flConcTime = args[1] ? atof(args[1]) : -1.0;
}

CON_COMMAND_F(ffdev_iclass, "Instantly switch classes", FCVAR_CHEAT)
{
	CFFPlayer *you = ToFFPlayer(UTIL_GetCommandClient());

	if (!args[1] || !args[1][0])
		return;

	you->InstaSwitch(atoi(args[1]));
}

CON_COMMAND(bot_infectme, "infects you")
{
	CFFPlayer *you = ToFFPlayer(UTIL_GetCommandClient());

	if (!you)
		return;

	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));

		if (pPlayer && (pPlayer->GetFlags() & FL_FAKECLIENT) && pPlayer->GetClassSlot() == CLASS_MEDIC)
		{
			you->Infect(pPlayer);
		}
	}
}

//-----------------------------------------------------------------------------
// Phase 2 (NextBot port): the CFFBot class moved to ff/bot/ff_bot.{cpp,h}.
// LINK_ENTITY_TO_CLASS( ff_bot, CFFBot ) now lives in ff_bot.cpp.
// BotPutInServer is a thin wrapper around CreateFFBot for backward compat
// (so existing scripts/cheats calling bot_add still work).
//-----------------------------------------------------------------------------
#include "bot/ff_bot.h"

CBasePlayer *BotPutInServer( bool bFrozen, int iTeam, int iClass, const char *pszCustomName )
{
	const int iEffectiveClass = bot_changeclass.GetInt() ? bot_changeclass.GetInt() : iClass;
	return CreateFFBot( bFrozen, iTeam, iEffectiveClass, pszCustomName );
}

// Handler for the "bot" command.
void BotAdd_f( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() )
		return;

	// The bot command uses switches like command-line switches.
	// -count <count> tells how many bots to spawn.
	// -team <index> selects the bot's team. Default is -1 which chooses randomly.
	//	Note: if you do -team !, then it 
	// -class <index> selects the bot's class. Default is -1 which chooses randomly.
	// -frozen prevents the bots from running around when they spawn in.
	// -name sets the bot's name

	// Look at -count.
	int count = args.FindArgInt( "-count", 1 );
	count = clamp( count, 1, 16 );

	if ( args.FindArg( "-all" ) )
		count = 9;

	// Look at -frozen.
	bool bFrozen = !!args.FindArg( "-frozen" );

	// Ok, spawn all the bots.
	while ( --count >= 0 )
	{
		// What class do they want?
		int iClass = RandomInt( CLASS_SCOUT, CLASS_CIVILIAN );
		char const *pVal = args.FindArg( "-class" );
		if ( pVal )
			iClass = Class_StringToInt( pVal );
			
		if ( args.FindArg( "-all" ) )
			iClass = 9 - count;

		int iTeam = TEAM_UNASSIGNED;
		pVal = args.FindArg( "-team" );
		if ( pVal )
		{
			if ( V_stricmp( pVal, "random" ) == 0 )
			{
				iTeam = RandomInt( FIRST_GAME_TEAM, GetNumberOfTeams() - 1 );
			}
			else
				iTeam = Team_StringToInt( pVal );
		}

		char const *pName = args.FindArg( "-name" );

		BotPutInServer( bFrozen, iTeam, iClass, pName );
	}
}
ConCommand cc_Bot( "bot_add", BotAdd_f, "Add a bot.", FCVAR_CHEAT );


//-----------------------------------------------------------------------------
// Phase 2: Bot_RunAll now delegates to NextBotManager. The CUserCmd-emitting
// AI lives inside each NextBot's components (locomotion/body/vision/intention).
// All the legacy random-walk helpers (Bot_Think, Bot_UpdateStrafing,
// Bot_UpdateDirection, Bot_FlipOut, Bot_HandleSendCmd, Bot_ForceFireWeapon,
// Bot_SetForwardMovement, Bot_HandleRespawn, Bot_RunMimicCommand,
// Bot_GhostThinkRecord/Playback, RunPlayerMove) have been removed — they
// referenced the old CFFBot : CFFPlayer random-walker class that no longer
// exists. If we ever want bot_mimic / ghost-record back as a debug feature,
// reintroduce them as a separate fake-client class that bypasses NextBot.
//-----------------------------------------------------------------------------
#include "NextBotManager.h"
#include "bot/ff_bot.h"

void Bot_RunAll( void )
{
	TheNextBots().Update();
	FFBotManager_Tick();
}


CON_COMMAND_F( bot_teleport, "Teleport the specified bot to the specified position & angles.\n\tFormat: bot_teleport <bot name> <X> <Y> <Z> <Pitch> <Yaw> <Roll>", FCVAR_CHEAT )
{
	const char* botname = args[1];

	if ( args.ArgC() < 2 )
	{
		CUtlVector< CBasePlayer* > botcandidates;

		CBasePlayer* pPlayer;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			pPlayer = ToBasePlayer(UTIL_PlayerByIndex(i));

			if (!pPlayer)
				continue;

			if (!pPlayer->IsFakeClient())
				continue;

			botcandidates.AddToTail(pPlayer);
		}

		if ( botcandidates.Count() > 0 )
		{
			int iRandom = RandomInt(0, botcandidates.Count() - 1);
			botname = botcandidates[iRandom]->GetPlayerName();
		}
	}

	if ( !botname )
	{
		Msg("No bot specified. bot_teleport <bot name> <X> <Y> <Z> <Pitch> <Yaw> <Roll>\n");
		return;
	}

	// get the bot's player object
	CBasePlayer *pBot = UTIL_PlayerByName( botname );
	if ( !pBot )
	{
		Msg( "No bot with name %s\n", botname );
		return;
	}

	Vector vecPos( vec3_origin );
	QAngle vecAng( vec3_angle );

	if ( args.ArgC() >= 5 )
	{
		vecPos = Vector( atof( args[2] ), atof( args[3] ), atof( args[4] ) );

		if ( args.ArgC() >= 8 )
		{
			vecAng = QAngle( atof( args[5] ), atof( args[6] ), atof( args[7] ) );
		}
	}
	else
	{
		CBasePlayer* pPlayer = UTIL_GetCommandClient();
		trace_t tr;
		Vector forward;
		pPlayer->EyeVectors( &forward );
		UTIL_TraceLine( pPlayer->EyePosition(), pPlayer->EyePosition() + forward * MAX_TRACE_LENGTH, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction != 1.0 )
		{
			vecPos = tr.endpos;
		}
	}

	pBot->Teleport( &vecPos, &vecAng, NULL );
}
