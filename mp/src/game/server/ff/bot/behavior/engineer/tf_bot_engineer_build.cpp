//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build.cpp
// Engineer building his buildings
// Michael Booth, February 2009

#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "ff_obj.h"
#include "ff_obj_sentrygun.h"
#include "ff_obj_dispenser.h"
#include "ff_gamerules.h"
#include "ff_weapon_builder.h"
#include "bot/ff_bot.h"
#include "bot/behavior/engineer/ff_bot_engineer_build.h"
#include "bot/behavior/engineer/ff_bot_engineer_build_teleport_entrance.h"
#include "bot/behavior/engineer/ff_bot_engineer_move_to_build.h"


#include "raid/ff_raid_logic.h"

// this was useful when engineers build at their normal (slow) rate to make sure initial sentries get built in time
ConVar ff_raid_engineer_infinte_metal( "ff_raid_engineer_infinte_metal", "1", FCVAR_CHEAT | FCVAR_DEVELOPMENTONLY );


//---------------------------------------------------------------------------------------------
Action< CFFBot > *CFFBotEngineerBuild::InitialContainedAction( CFFBot *me )
{
	if ( FFGameRules()->IsPVEModeActive() )
	{
		return new CFFBotEngineerMoveToBuild;
	}

	return new CFFBotEngineerBuildTeleportEntrance;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEngineerBuild::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEngineerBuild::Update( CFFBot *me, float interval )
{
	if ( FFGameRules()->IsPVEModeActive() && ff_raid_engineer_infinte_metal.GetBool() )
	{
		// infinite ammo
		me->GiveAmmo( 1000, TF_AMMO_METAL, true );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerBuild::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEngineerBuild::OnTerritoryLost( CFFBot *me, int territoryID )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
// Hack to disable ammo/health gathering elsewhere
QueryResultType CFFBotEngineerBuild::ShouldHurry( const INextBot *meBot ) const
{
	CFFBot *me = (CFFBot *)meBot->GetEntity();

	CObjectSentrygun *mySentry = (CObjectSentrygun *)me->GetObjectOfType( OBJ_SENTRYGUN );
	CObjectDispenser *myDispenser = (CObjectDispenser *)me->GetObjectOfType( OBJ_DISPENSER );

	if ( mySentry && myDispenser && !mySentry->IsBuilding() && !myDispenser->IsBuilding() && me->GetActiveFFWeapon() && me->GetActiveFFWeapon()->GetWeaponID() == FF_WEAPON_WRENCH )
	{
		if ( me->IsAmmoLow() && myDispenser->GetAvailableMetal() <= 0 )
		{
			// we're totally out of metal - collect some nearby
			return ANSWER_NO;
		}

		// by being in a "hurry" we wont collect health and ammo
		return ANSWER_YES;
	}

	return ANSWER_UNDEFINED;
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotEngineerBuild::ShouldAttack( const INextBot *meBot, const CKnownEntity *them ) const
{
	CFFBot *me = (CFFBot *)meBot->GetEntity();
	CObjectSentrygun *mySentry = (CObjectSentrygun *)me->GetObjectOfType( OBJ_SENTRYGUN );

	CFFPlayer *themPlayer = ToFFPlayer( them->GetEntity() );

	if ( themPlayer && themPlayer->IsPlayerClass( CLASS_SPY ) )
	{
		// Engineers hate Spies
		return ANSWER_YES;
	}

	if ( mySentry && me->IsRangeLessThan( mySentry, 100.0f ) )
	{
		// focus on keeping our sentry alive
		return ANSWER_NO;
	}

	return ANSWER_UNDEFINED;
}
