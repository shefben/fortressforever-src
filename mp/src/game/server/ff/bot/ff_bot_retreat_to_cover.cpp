//========= Fortress Forever Bot =============================================//
//
// CFFBotRetreatToCover — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_retreat_to_cover.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "nav_pathfind.h"
#include "NextBotKnownEntity.h"
#include "NextBotInterface.h"
#include "NextBotLocomotionInterface.h"
#include "NextBotVisionInterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_retreat_to_cover_range( "ff_bot_retreat_to_cover_range", "1000", FCVAR_CHEAT );
ConVar ff_bot_wait_in_cover_min_time( "ff_bot_wait_in_cover_min_time", "1", FCVAR_CHEAT );
ConVar ff_bot_wait_in_cover_max_time( "ff_bot_wait_in_cover_max_time", "2", FCVAR_CHEAT );


//-----------------------------------------------------------------------------
// True if a hitscan from `from` to `to` is unobstructed (NULL filter — we
// only care about world geometry blocking, not players/buildings).
//-----------------------------------------------------------------------------
static bool IsAreaPairVisible( const Vector &from, const Vector &to )
{
	trace_t tr;
	UTIL_TraceLine( from + Vector( 0, 0, 50.0f ), to + Vector( 0, 0, 50.0f ),
		MASK_VISIBLE_AND_NPCS, NULL, COLLISION_GROUP_NONE, &tr );
	return tr.fraction >= 1.0f && !tr.startsolid;
}


//-----------------------------------------------------------------------------
// Tests one nav area against all known threats, counting how many threats can
// "see" the area. Lower exposure = better cover.
//-----------------------------------------------------------------------------
class CFFTestAreaAgainstThreats : public IVision::IForEachKnownEntity
{
public:
	CFFTestAreaAgainstThreats( CFFBot *me, CFFNavArea *area )
		: m_me( me ), m_area( area ), m_exposedThreatCount( 0 )
	{
	}

	virtual bool Inspect( const CKnownEntity &known ) OVERRIDE
	{
		if ( !known.GetEntity() )
			return true;

		// Friendly? skip.
		CBaseCombatCharacter *threatBcc = known.GetEntity()->MyCombatCharacterPointer();
		if ( !threatBcc )
			return true;
		if ( threatBcc->GetTeamNumber() == m_me->GetTeamNumber() )
			return true;

		// Trace from threat's last-known position to this area's center.
		// If clear, the area is exposed to this threat.
		const Vector threatPos = known.GetLastKnownPosition();
		if ( IsAreaPairVisible( threatPos, m_area->GetCenter() ) )
			++m_exposedThreatCount;

		return true;
	}

	CFFBot *m_me;
	CFFNavArea *m_area;
	int m_exposedThreatCount;
};


//-----------------------------------------------------------------------------
// Search outward from the bot's area, scoring each area by exposure.
// Collects the lowest-exposure tier of areas in m_coverAreaVector.
//-----------------------------------------------------------------------------
class CFFSearchForCover : public ISearchSurroundingAreasFunctor
{
public:
	CFFSearchForCover( CFFBot *me )
		: m_me( me ), m_minExposureCount( INT_MAX )
	{
	}

	virtual bool operator() ( CNavArea *baseArea, CNavArea *priorArea, float travelDistanceSoFar ) OVERRIDE
	{
		CFFNavArea *area = static_cast< CFFNavArea * >( baseArea );

		CFFTestAreaAgainstThreats test( m_me, area );
		m_me->GetVisionInterface()->ForEachKnownEntity( test );

		if ( test.m_exposedThreatCount <= m_minExposureCount )
		{
			if ( test.m_exposedThreatCount < m_minExposureCount )
			{
				m_coverAreaVector.RemoveAll();
				m_minExposureCount = test.m_exposedThreatCount;
			}
			m_coverAreaVector.AddToTail( area );
		}

		return true;
	}

	virtual bool ShouldSearch( CNavArea *adjArea, CNavArea *currentArea, float travelDistanceSoFar ) OVERRIDE
	{
		if ( travelDistanceSoFar > ff_bot_retreat_to_cover_range.GetFloat() )
			return false;

		// Allow falling off ledges, but don't jump up — too slow when fleeing.
		ILocomotion *loco = m_me->GetLocomotionInterface();
		const float stepHeight = loco ? loco->GetStepHeight() : 18.0f;
		return ( currentArea->ComputeAdjacentConnectionHeightChange( adjArea ) < stepHeight );
	}

	CFFBot *m_me;
	CUtlVector< CFFNavArea * > m_coverAreaVector;
	int m_minExposureCount;
};


//-----------------------------------------------------------------------------
CFFBotRetreatToCover::CFFBotRetreatToCover( float hideDuration )
{
	m_hideDuration = hideDuration;
	m_actionToChangeToOnceCoverReached = NULL;
	m_coverArea = NULL;
}


CFFBotRetreatToCover::CFFBotRetreatToCover( Action< CFFBot > *actionToChangeToOnceCoverReached )
{
	m_hideDuration = -1.0f;
	m_actionToChangeToOnceCoverReached = actionToChangeToOnceCoverReached;
	m_coverArea = NULL;
}


//-----------------------------------------------------------------------------
CFFNavArea *CFFBotRetreatToCover::FindCoverArea( CFFBot *me )
{
	CNavArea *startArea = me->GetLastKnownArea();
	if ( !startArea )
		return NULL;

	CFFSearchForCover search( me );
	SearchSurroundingAreas( startArea, search );

	if ( search.m_coverAreaVector.Count() == 0 )
		return NULL;

	// Pick from the closest 10 areas to avoid the whole team bunching up
	// in one spot. SearchSurroundingAreas yields in BFS order, so the
	// first entries are nearest.
	const int last = MIN( 10, search.m_coverAreaVector.Count() );
	const int which = RandomInt( 0, last - 1 );
	return search.m_coverAreaVector[ which ];
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotRetreatToCover::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	m_coverArea = FindCoverArea( me );
	if ( m_coverArea == NULL )
		return Done( "No cover available" );

	if ( m_hideDuration < 0.0f )
	{
		m_hideDuration = RandomFloat(
			ff_bot_wait_in_cover_min_time.GetFloat(),
			ff_bot_wait_in_cover_max_time.GetFloat() );
	}
	m_waitInCoverTimer.Start( m_hideDuration );

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotRetreatToCover::Update( CFFBot *me, float interval )
{
	IVision *vision = me->GetVisionInterface();
	if ( !vision )
		return Done( "No vision" );

	const CKnownEntity *threat = vision->GetPrimaryKnownThreat( true );

	// Continue attacking while retreating.
	me->EquipBestWeaponForThreat( threat );

	// In cover, or no visible threats — opportunistic stop.
	if ( me->GetLastKnownArea() == m_coverArea || !threat )
	{
		if ( threat )
		{
			// Threats still visible from here — find new cover.
			m_coverArea = FindCoverArea( me );
			if ( m_coverArea == NULL )
				return Done( "Cover became exposed; no other cover available" );
		}

		if ( m_actionToChangeToOnceCoverReached )
			return ChangeTo( m_actionToChangeToOnceCoverReached, "In cover, switching to follow-up action" );

		if ( m_waitInCoverTimer.IsElapsed() )
			return Done( "Hidden long enough" );
	}
	else
	{
		// Not in cover yet — keep moving.
		m_waitInCoverTimer.Reset();

		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.3f, 0.5f ) );
			// FIX 7 — repath hysteresis: leave a working path alone while the bot
			// is actually travelling it. Volatile cost terms (combat intensity,
			// grenade danger, ammo/health discounts, recent-stuck penalties) used
			// to flip A* between near-equal lanes on consecutive repaths.
			if ( FFBotHelpers::ShouldRecomputePath( me, m_path, m_coverArea->GetCenter() ) )
			{
				CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
				m_path.Compute( me, m_coverArea->GetCenter(), cost );
			}
		}
		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotRetreatToCover::OnStuck( CFFBot *me )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotRetreatToCover::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotRetreatToCover::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotRetreatToCover::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_YES;
}
