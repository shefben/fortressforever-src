//========= Copyright Valve Corporation, All rights reserved. ============//
// Michael Booth, September 2012

#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "ff_obj.h"
#include "ff_obj_sentrygun.h"
#include "ff_obj_dispenser.h"
#include "ff_gamerules.h"
#include "ff_weapon_builder.h"
#include "bot/ff_bot.h"
#include "bot/behavior/engineer/mvm_engineer/ff_bot_mvm_engineer_build_sentry.h"
#include "bot/map_entities/ff_bot_hint_sentrygun.h"
#include "bot/map_entities/ff_bot_hint_teleporter_exit.h"
#include "string_t.h"
#include "ff_fx.h"

extern ConVar ff_bot_engineer_mvm_building_health_multiplier;

//---------------------------------------------------------------------------------------------
CFFBotMvMEngineerBuildSentryGun::CFFBotMvMEngineerBuildSentryGun( CFFBotHintSentrygun* pSentryHint )
{
	m_sentryBuildHint = pSentryHint;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMvMEngineerBuildSentryGun::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	me->StartBuildingObjectOfType( OBJ_SENTRYGUN );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMvMEngineerBuildSentryGun::Update( CFFBot *me, float interval )
{
	if ( m_sentryBuildHint == NULL )
		return Done( "No hint entity" );

	float rangeToBuildSpot = me->GetRangeTo( m_sentryBuildHint->GetAbsOrigin() );

	if ( rangeToBuildSpot < 200.0f )
	{
		// crouch as we get close so we don't overshoot
		me->PressCrouchButton();

		me->GetBodyInterface()->AimHeadTowards( m_sentryBuildHint->GetAbsOrigin(), IBody::MANDATORY, 0.1f, NULL, "Placing sentry" );
	}

	// various interruptions could mean we're away from our build location - move to it
	if ( rangeToBuildSpot > 25.0f )
	{
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );

			CFFBotPathCost cost( me, SAFEST_ROUTE );
			m_path.Compute( me, m_sentryBuildHint->GetAbsOrigin(), cost );
		}

		m_path.Update( me );

		if ( !m_path.IsValid() )
		{
			return Done( "Path failed" );
		}

		return Continue();
	}

	if ( !m_delayBuildTime.HasStarted() )
	{
		m_delayBuildTime.Start( 0.1f );
		FFGameRules()->PushAllPlayersAway( m_sentryBuildHint->GetAbsOrigin(), 400, 500, FF_TEAM_RED );
	}
	else if ( m_delayBuildTime.HasStarted() && m_delayBuildTime.IsElapsed() )
	{
		// destroy previous object
		me->DetonateObjectOfType( OBJ_SENTRYGUN, MODE_SENTRYGUN_NORMAL, true );

		// directly create a sentry gun at the precise position and orientation desired
		m_sentry = (CObjectSentrygun *)CreateEntityByName( "obj_sentrygun" );
		if ( m_sentry )
		{
			m_sentry->SetName( m_sentryBuildHint->GetEntityName() );

			m_sentryBuildHint->IncrementUseCount();
			m_sentry->m_nDefaultUpgradeLevel = 2;

			m_sentry->SetAbsOrigin( m_sentryBuildHint->GetAbsOrigin() );
			m_sentry->SetAbsAngles( QAngle( 0, m_sentryBuildHint->GetAbsAngles().y, 0 ) );
			m_sentry->Spawn();

			m_sentry->StartPlacement( me );
			m_sentry->StartBuilding( me );

			// the sentry owns this hint now
			m_sentryBuildHint->SetOwnerEntity( m_sentry );

			m_sentry = NULL;
		}

		return Done( "Built a sentry" );
	}

	return Continue();
}



//---------------------------------------------------------------------------------------------
void CFFBotMvMEngineerBuildSentryGun::OnEnd( CFFBot *me, Action< CFFBot > *nextAction )
{
	if ( m_sentry.Get() )
	{
		m_sentry->DropCarriedObject( me );
		UTIL_Remove( m_sentry );
		m_sentry = NULL;
	}
}
