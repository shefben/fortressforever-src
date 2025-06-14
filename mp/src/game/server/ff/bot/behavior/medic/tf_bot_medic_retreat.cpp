//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_retreat.cpp
// Retreat towards our spawn to find another patient
// Michael Booth, May 2009

#include "cbase.h"
#include "ff_player.h"
#include "ff_gamerules.h"
#include "ff_weapon_medigun.h"
#include "bot/ff_bot.h"
#include "bot/behavior/medic/ff_bot_medic_retreat.h"

#include "nav_mesh.h"

extern ConVar ff_bot_path_lookahead_range;
extern ConVar ff_bot_medic_follow_range;
extern ConVar ff_bot_force_class;


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMedicRetreat::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	CTFNavArea *homeArea = me->GetSpawnArea();

	if ( homeArea == NULL )
	{
		return Done( "No home area!" );
	}

	m_path.SetMinLookAheadDistance( ff_bot_path_lookahead_range.GetFloat() );

	CFFBotPathCost cost( me, FASTEST_ROUTE );
	m_path.Compute( me, homeArea->GetCenter(), cost );

	return Continue();
}


//---------------------------------------------------------------------------------------------
class CUsefulHealTargetFilter : public INextBotEntityFilter
{
public:
	CUsefulHealTargetFilter( int team )
	{
		m_team = team;
	}

	virtual bool IsAllowed( CBaseEntity *entity ) const
	{
		if ( entity && entity->IsPlayer() && entity->GetTeamNumber() == m_team )
		{
			return !ToTFPlayer( entity )->IsPlayerClass( CLASS_MEDIC ) && !ToTFPlayer( entity )->IsPlayerClass( CLASS_SNIPER );
		}
		return false;
	}

	int m_team;
};


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMedicRetreat::Update( CFFBot *me, float interval )
{
	// equip the syringegun and defend ourselves!
	CTFWeaponBase *myWeapon = me->m_Shared.GetActiveTFWeapon();
	if ( myWeapon )
	{
		if ( myWeapon->GetWeaponID() != FF_WEAPON_SYRINGEGUN_MEDIC )
		{
			CBaseCombatWeapon *syringeGun = me->Weapon_GetSlot( TF_WPN_TYPE_PRIMARY );

			if ( syringeGun )
			{
				me->Weapon_Switch( syringeGun );
			}
		}
	}

	m_path.Update( me );

	// look around to try to spot a friend to heal
	if ( m_lookAroundTimer.IsElapsed() )
	{
		m_lookAroundTimer.Start( RandomFloat( 0.33f, 1.0f ) );

		QAngle angle;
		angle.x = 0.0f;
		angle.y = RandomFloat( -180.0f, 180.0f );
		angle.z = 0.0f;

		Vector forward;
		AngleVectors( angle, &forward );

		me->GetBodyInterface()->AimHeadTowards( me->EyePosition() + forward, IBody::IMPORTANT, 0.1f, NULL, "Looking for someone to heal" );
	}

	// if we see a friend, heal them
	CUsefulHealTargetFilter filter( me->GetTeamNumber() );
	const CKnownEntity *known = me->GetVisionInterface()->GetClosestKnown( filter );
	if ( known )
	{
		return Done( "I know of a teammate" );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMedicRetreat::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	CFFBotPathCost cost( me, FASTEST_ROUTE );
	m_path.Compute( me, me->GetSpawnArea()->GetCenter(), cost );

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMedicRetreat::OnStuck( CFFBot *me )
{
	CFFBotPathCost cost( me, FASTEST_ROUTE );
	m_path.Compute( me, me->GetSpawnArea()->GetCenter(), cost );

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMedicRetreat::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	CFFBotPathCost cost( me, FASTEST_ROUTE );
	m_path.Compute( me, me->GetSpawnArea()->GetCenter(), cost );

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotMedicRetreat::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	// defend ourselves!
	return ANSWER_YES;
}
