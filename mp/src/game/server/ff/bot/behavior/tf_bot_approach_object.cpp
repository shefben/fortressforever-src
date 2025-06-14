//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_approach_object.h
// Move near/onto an object
// Michael Booth, February 2009

#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_approach_object.h"

extern ConVar ff_bot_path_lookahead_range;

//---------------------------------------------------------------------------------------------
CFFBotApproachObject::CFFBotApproachObject( CBaseEntity *loot, float range )
{
	m_loot = loot;
	m_range = range;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotApproachObject::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotApproachObject::Update( CFFBot *me, float interval )
{
	if ( m_loot == NULL )
	{
		return Done( "Object is NULL" );
	}

	if ( m_loot->IsEffectActive( EF_NODRAW ) )
	{
		return Done( "Object is NODRAW" );
	}

	if ( me->GetLocomotionInterface()->GetGround() == m_loot )
	{
		return Done( "I'm standing on the object" );
	}

	if ( me->IsDistanceBetweenLessThan( m_loot->GetAbsOrigin(), m_range ) )
	{
		// in case we can't pick up the loot for some reason
		return Done( "Reached object" );
	}

	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );

		CFFBotPathCost cost( me, FASTEST_ROUTE );
		m_path.Compute( me, m_loot->GetAbsOrigin(), cost );
	}

	// move to the loot
	m_path.Update( me );

	return Continue();
}


