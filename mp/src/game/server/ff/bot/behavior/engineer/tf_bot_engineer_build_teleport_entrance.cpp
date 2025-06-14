//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_engineer_build_teleport_entrance.cpp
// Engineer building a teleport entrance right outside of the spawn room
// Michael Booth, May 2009

#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "ff_obj.h"
#include "ff_weapon_builder.h"
#include "bot/ff_bot.h"
#include "bot/behavior/engineer/ff_bot_engineer_build_teleport_entrance.h"
#include "bot/behavior/engineer/ff_bot_engineer_move_to_build.h"
#include "bot/behavior/ff_bot_get_ammo.h"

extern ConVar ff_bot_path_lookahead_range;

ConVar ff_bot_max_teleport_entrance_travel( "ff_bot_max_teleport_entrance_travel", "1500", FCVAR_CHEAT, "Don't plant teleport entrances farther than this travel distance from our spawn room" );
ConVar ff_bot_teleport_build_surface_normal_limit( "ff_bot_teleport_build_surface_normal_limit", "0.99", FCVAR_CHEAT, "If the ground normal Z component is less that this value, Engineer bots won't place their entrance teleporter" );

//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEngineerBuildTeleportEntrance::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEngineerBuildTeleportEntrance::Update( CFFBot *me, float interval )
{
	CTeamControlPoint *point = me->GetMyControlPoint();
	if ( !point )
	{
		// wait until a control point becomes available
		return Continue();
	}

	CTFNavArea *myArea = me->GetLastKnownArea();

	if ( !myArea )
	{
		return Done( "No nav mesh!" );
	}

	if ( myArea->GetIncursionDistance( me->GetTeamNumber() ) > ff_bot_max_teleport_entrance_travel.GetFloat() )
	{
		return ChangeTo( new CFFBotEngineerMoveToBuild, "Too far from our spawn room to build teleporter entrance" );
	}

	// make sure we go back to our resupply cabinet after planting the teleporter entrance before we move on
	if ( !me->IsAmmoFull() && CFFBotGetAmmo::IsPossible( me ) )
	{
		return SuspendFor( new CFFBotGetAmmo, "Refilling ammo" );
	}

	CBaseObject	*myTeleportEntrance = me->GetObjectOfType( OBJ_TELEPORTER, MODE_TELEPORTER_ENTRANCE );
	if ( myTeleportEntrance )
	{
		// successfully built
		return ChangeTo( new CFFBotEngineerMoveToBuild, "Teleport entrance built" );
	}

	// head towards the control point and build as soon as we can
	if ( !m_path.IsValid() )
	{
		CFFBotPathCost cost( me, FASTEST_ROUTE );
		m_path.Compute( me, point->GetAbsOrigin(), cost );
	}

	m_path.Update( me );

	// build
	CFFWeaponBase *myGun = me->GetActiveFFWeapon();
	if ( myGun )
	{
		CFFWeaponBase *builder = dynamic_cast< CFFWeaponBase * >( myGun );
		if ( builder )
		{
			// don't build on slopes - causes player blockages
			Vector forward;
			me->EyeVectors( &forward );

			const float placementRange = 30.0f;
			forward *= placementRange;

			trace_t result;
			UTIL_TraceLine( me->WorldSpaceCenter() + Vector( forward.x, forward.y, 0.0f ), me->WorldSpaceCenter() + Vector( forward.x, forward.y, -200.0f ), MASK_PLAYERSOLID, me, COLLISION_GROUP_NONE, &result );

			if ( builder->IsValidPlacement() && result.DidHit() && result.plane.normal.z > ff_bot_teleport_build_surface_normal_limit.GetFloat() )
			{
				// place it down
				me->PressFireButton();
			}
		}
		else
		{
			// switch to teleporter builder
			me->StartBuildingObjectOfType( OBJ_TELEPORTER, MODE_TELEPORTER_ENTRANCE );
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEngineerBuildTeleportEntrance::OnStuck( CFFBot *me )
{
	m_path.Invalidate();

	return TryContinue();
}
