//========= Fortress Forever Bot =============================================//
//
// CFFBotSpySap — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_spy_sap.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_bot_spy_attack.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "ff_buildable_dispenser.h"
#include "ff_player.h"
#include "entitylist.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// FF sabotage requires looking at the buildable from <100u (engine constant
// in CFFPlayer::SpySabotageThink). Approach to slightly less so the look
// trace stays comfortable.
#define FFBOT_SAP_RANGE			80.0f
#define FFBOT_SAP_HOLD_RANGE	60.0f


//-----------------------------------------------------------------------------
CFFBuildableObject *CFFBotSpySap::FindNearestSappableTarget( CFFBot *me )
{
	if ( !me )
		return NULL;

	const Vector myPos = me->GetAbsOrigin();
	const int myTeam = me->GetTeamNumber();

	CFFBuildableObject *best = NULL;
	float bestDistSq = FLT_MAX;

	// Walk both buildable types. Sentries are higher priority (they kill us)
	// but we'll let raw distance pick — caller can re-prioritize.
	const char *classnames[] = { "FF_SentryGun", "FF_Dispenser" };
	for ( int c = 0; c < 2; ++c )
	{
		CBaseEntity *e = NULL;
		while ( ( e = gEntList.FindEntityByClassname( e, classnames[ c ] ) ) != NULL )
		{
			CFFBuildableObject *b = static_cast< CFFBuildableObject * >( e );
			if ( !b->IsBuilt() )
				continue;
			if ( b->GetTeamNumber() == myTeam )
				continue;
			if ( !b->CanSabotage() )
				continue;
			if ( b->IsSabotaged() )
				continue;	// already done

			const float dSq = ( b->GetAbsOrigin() - myPos ).LengthSqr();
			if ( dSq < bestDistSq )
			{
				bestDistSq = dSq;
				best = b;
			}
		}
	}

	return best;
}


//-----------------------------------------------------------------------------
CFFBotSpySap::CFFBotSpySap( CFFBuildableObject *sapTarget )
{
	m_sapTarget = sapTarget;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpySap::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	if ( m_sapTarget == NULL )
	{
		m_sapTarget = FindNearestSappableTarget( me );
		if ( m_sapTarget == NULL )
			return Done( "No sappable target" );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpySap::Update( CFFBot *me, float interval )
{
	// Lost target?
	if ( m_sapTarget == NULL || !m_sapTarget->IsBuilt() )
	{
		CFFBuildableObject *next = FindNearestSappableTarget( me );
		if ( next )
		{
			m_sapTarget = next;
		}
		else
		{
			return Done( "Sap target gone, no others" );
		}
	}

	// Target was successfully sabotaged — chain to next or finish.
	if ( m_sapTarget->IsSabotaged() )
	{
		CFFBuildableObject *next = FindNearestSappableTarget( me );
		if ( next )
		{
			m_sapTarget = next;
			m_path.Invalidate();
		}
		else
		{
			// All known enemy buildings sabotaged — release the sabotage so
			// sentries shoot teammates / dispensers explode.
			if ( me->AnyActiveSentrySabotages() || me->AnyActiveDispenserSabotages() )
			{
				me->SpySabotageRelease();
			}
			return Done( "All enemy buildings sabotaged" );
		}
	}

	// Opportunistic backstab: if an enemy engineer is between me and the
	// target (and within 150u), suspend to backstab them first — losing the
	// owner is half the sap value.
	IVision *vision = me->GetVisionInterface();
	if ( vision )
	{
		CUtlVector< CKnownEntity > known;
		vision->CollectKnownEntities( &known );
		for ( int i = 0; i < known.Count(); ++i )
		{
			CFFPlayer *pp = ToFFPlayer( known[ i ].GetEntity() );
			if ( !pp || pp->GetClassSlot() != CLASS_ENGINEER )
				continue;
			if ( pp->GetTeamNumber() == me->GetTeamNumber() )
				continue;
			if ( me->IsRangeLessThan( pp, 150.0f ) )
			{
				return SuspendFor( new CFFBotSpyAttack( pp ),
					"Backstabbing engineer before sapping" );
			}
		}
	}

	const float distSq = ( me->GetAbsOrigin() - m_sapTarget->GetAbsOrigin() ).LengthSqr();

	if ( distSq <= FFBOT_SAP_HOLD_RANGE * FFBOT_SAP_HOLD_RANGE )
	{
		// Within sap range — must be stationary AND looking at target. The
		// engine's SpySabotageThink runs the timer for us when these hold.
		IBody *body = me->GetBodyInterface();
		if ( body )
		{
			body->AimHeadTowards( m_sapTarget, IBody::MANDATORY, 0.5f, NULL,
				"Looking at sap target" );
		}

		// Don't keep walking — velocity > 100u/s aborts the sabotage timer.
		// PathFollower's Approach will keep pushing us forward unless we
		// invalidate the path.
		m_path.Invalidate();
		return Continue();
	}

	// Approach the target.
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
		CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
		if ( !m_path.Compute( me, m_sapTarget->WorldSpaceCenter(), cost ) )
			return Done( "No path to sap target" );
	}

	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );
	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSpySap::OnStuck( CFFBot *me )
{
	return TryDone( RESULT_CRITICAL, "Stuck while sapping" );
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpySap::ShouldAttack( const INextBot *meBot, const CKnownEntity *them ) const
{
	// Don't shoot while approaching the target — keeps us subtle. If our
	// target is already sabotaged, the parent will pick a new behavior.
	return ANSWER_NO;
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpySap::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotSpySap::IsHindrance( const INextBot *me, CBaseEntity *blocker ) const
{
	if ( m_sapTarget.Get() && me->IsRangeLessThan( m_sapTarget, 300.0f ) )
		return ANSWER_NO;	// closing — don't dodge anyone
	return ANSWER_UNDEFINED;
}
