//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_hide.cpp
// Move to a hiding spot
// Michael Booth, September 2011

#include "cbase.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/spy/ff_bot_spy_hide.h"
#include "bot/behavior/spy/ff_bot_spy_lurk.h"
#include "bot/behavior/spy/ff_bot_spy_attack.h"


//---------------------------------------------------------------------------------------------
CFFBotSpyHide::CFFBotSpyHide( CTFPlayer *victim )
{
	m_initialVictim = victim;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSpyHide::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_hidingSpot = NULL;
	m_findTimer.Invalidate();
	m_isAtGoal = false;

	CTFNavArea *myArea = me->GetLastKnownArea();

	int enemyTeam = GetEnemyTeam( me->GetTeamNumber() );

	m_incursionThreshold = myArea ? myArea->GetIncursionDistance( enemyTeam ) : FLT_MAX;
	if ( m_incursionThreshold < 0.0f )
	{
		m_incursionThreshold = FLT_MAX;
	}

	m_talkTimer.Start( RandomFloat( 5.0f, 10.0f ) );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSpyHide::Update( CFFBot *me, float interval )
{
	if ( m_initialVictim != NULL && !me->GetVisionInterface()->IsIgnored( m_initialVictim ) )
	{
		return SuspendFor( new CFFBotSpyAttack( m_initialVictim ), "Going after our initial victim" );
	}

	// go after victims we've gotten behind
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->GetTimeSinceLastKnown() < 3.0f )
	{
		CTFPlayer *victim = ToTFPlayer( threat->GetEntity() );
		if ( victim )
		{
			const float attackRange = 750.0f;
			if ( me->IsRangeLessThan( victim, attackRange ) )
			{
				if ( !victim->IsLookingTowards( me ) || victim->IsFiringWeapon() )
				{
					return SuspendFor( new CFFBotSpyAttack( victim ), "Opportunistic attack or self defense!" );
				}
			}
		}
	}

	if ( m_talkTimer.IsElapsed() )
	{
		m_talkTimer.Start( RandomFloat( 5.0f, 10.0f ) );
		me->EmitSound( "Spy.TeaseVictim" );
	}

	if ( m_isAtGoal )
	{
		// Quiet everyone! We are hiding now!
		CTFNavArea *myArea = me->GetLastKnownArea();
		if ( myArea )
		{
			int enemyTeam = GetEnemyTeam( me->GetTeamNumber() );

	  		m_incursionThreshold = myArea->GetIncursionDistance( enemyTeam );
		}

		return SuspendFor( new CFFBotSpyLurk, "Reached hiding spot - lurking" );
	}

	if ( m_hidingSpot == NULL && m_findTimer.IsElapsed() )
	{
		FindHidingSpot( me );
	}

	// move to our hiding spot
	m_path.Update( me );

	// path following may invalidate our hiding spot (OnMoveToFailure())
	if ( m_hidingSpot == NULL )
	{
		return Continue();
	}

	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 0.3f, 0.5f ) );

		CFFBotPathCost cost( me, SAFEST_ROUTE );
		m_path.Compute( me, m_hidingSpot->GetPosition(), cost );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotSpyHide::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_hidingSpot = NULL;
	m_isAtGoal = false;
	m_initialVictim = NULL;

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSpyHide::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	m_isAtGoal = true;

	return TryContinue( RESULT_CRITICAL );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotSpyHide::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	m_hidingSpot = NULL;
	m_isAtGoal = false;

	return TryContinue( RESULT_IMPORTANT );
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotSpyHide::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	return ANSWER_NO;
}

struct IncursionEntry_t
{
	int team;
	CTFNavArea *area;
};

//---------------------------------------------------------------------------------------------
class SpyHideIncursionDistanceLess
{
public:
	bool Less( const IncursionEntry_t &src1, const IncursionEntry_t &src2, void *pCtx )
	{
		return src1.area->GetIncursionDistance( src1.team ) < src2.area->GetIncursionDistance( src2.team );
	}
};


//---------------------------------------------------------------------------------------------
bool CFFBotSpyHide::FindHidingSpot( CFFBot *me )
{
	CTFNavArea *myArea = me->GetLastKnownArea();
	if ( !myArea )
	{
		return false;
	}

	m_hidingSpot = NULL;

	// find a spot to hide
	const float maxRange = 3500.0f;
	CUtlVector< CNavArea * > nearbyVector;
	CollectSurroundingAreas( &nearbyVector, me->GetLastKnownArea(), maxRange, 
							 500.0f, 500.0f );

	CUtlSortVector< IncursionEntry_t, SpyHideIncursionDistanceLess > hidingSpotVector;

	float maxIncursion = m_incursionThreshold + 1000.0f;

	int enemyTeam = GetEnemyTeam( me->GetTeamNumber() );

	// if we are standing in an area the defenders can't reach, don't limit
	if ( myArea->GetIncursionDistance( enemyTeam ) < 0.0f )
	{
		maxIncursion = 9999999;
	}

	for( int i=0; i<nearbyVector.Count(); ++i )
	{
		CTFNavArea *area = (CTFNavArea *)nearbyVector[i];

		if ( area->GetHidingSpots()->Count() <= 0 )
		{
			continue;
		}

		if ( area->GetIncursionDistance( enemyTeam ) < 0 )
		{
			continue;
		}

		// keep pushing inwards towards defender's spawn
		if ( area->GetIncursionDistance( enemyTeam ) > maxIncursion )
		{
			continue;
		}

		IncursionEntry_t entry = { enemyTeam, area };
		hidingSpotVector.Insert( entry );
	}

	if ( hidingSpotVector.Count() <= 0 )
	{
		return false;
	}

	// penetrate as far as we can
	int which = RandomInt( 0, hidingSpotVector.Count()/2 );
	CTFNavArea *whichArea = hidingSpotVector[ which ].area;

	const HidingSpotVector *hidingSpots = whichArea->GetHidingSpots();

	m_hidingSpot = hidingSpots->Element( RandomInt( 0, hidingSpots->Count()-1 ) );

	return true;
}
