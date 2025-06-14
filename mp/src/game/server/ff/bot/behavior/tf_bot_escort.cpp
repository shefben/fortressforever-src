//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_escort.cpp
// Move near an entity and protect it
// Michael Booth, April 2011

#include "cbase.h"

#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_escort.h"
#include "bot/behavior/ff_bot_attack.h"
#include "bot/behavior/demoman/ff_bot_prepare_stickybomb_trap.h"
#include "bot/behavior/ff_bot_destroy_enemy_sentry.h"

#include "nav_mesh.h"

extern ConVar ff_bot_path_lookahead_range;

ConVar ff_bot_escort_range( "ff_bot_escort_range", "300", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
CFFBotEscort::CFFBotEscort( CBaseEntity *who )
{
	SetWho( who );
}


//---------------------------------------------------------------------------------------------
void CFFBotEscort::SetWho( CBaseEntity *who )
{
	m_who = who;
}


//---------------------------------------------------------------------------------------------
CBaseEntity *CFFBotEscort::GetWho( void ) const
{
	return m_who;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEscort::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_pathToWho.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotEscort::Update( CFFBot *me, float interval )
{
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->IsVisibleInFOVNow() )
	{
		return SuspendFor( new CFFBotAttack, "Attacking nearby threat" );
	}
	else
	{
		// no enemy is visible - move near who we are escorting
		if ( m_who != NULL )
		{
			if ( me->IsRangeGreaterThan( m_who, ff_bot_escort_range.GetFloat() ) )
			{
				if ( m_repathTimer.IsElapsed() )
				{
					CFFBotPathCost cost( me, FASTEST_ROUTE );
					m_pathToWho.Compute( me, m_who->GetAbsOrigin(), cost );
					m_repathTimer.Start( RandomFloat( 2.0f, 3.0f ) );
				}

				m_pathToWho.Update( me );
			}
			else
			{
				if ( CFFBotPrepareStickybombTrap::IsPossible( me ) )
				{
					return SuspendFor( new CFFBotPrepareStickybombTrap, "Laying sticky bombs!" );
				}
			}
		}

		// destroy enemy sentry guns we've encountered
		if ( me->GetEnemySentry() && CFFBotDestroyEnemySentry::IsPossible( me ) )
		{
			return SuspendFor( new CFFBotDestroyEnemySentry, "Going after an enemy sentry to destroy it" );
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEscort::OnStuck( CFFBot *me )
{
	m_repathTimer.Invalidate();

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEscort::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEscort::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryContinue();
}


//---------------------------------------------------------------------------------------------
QueryResultType	CFFBotEscort::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotEscort::OnCommandApproach( CFFBot *me, const Vector &pos, float range )
{
	return TryContinue();
}
