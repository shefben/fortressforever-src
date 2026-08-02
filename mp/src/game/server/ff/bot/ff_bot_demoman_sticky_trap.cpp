//========= Fortress Forever Bot =============================================//
//
// CFFBotDemomanStickyTrap — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_demoman_sticky_trap.h"
#include "ff_nav_area.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Cap on stickies in a single trap.
#define FFBOT_MAX_STICKIES_IN_TRAP	6
#define FFBOT_MIN_STICKIES_IN_TRAP	4


//-----------------------------------------------------------------------------
bool CFFBotDemomanStickyTrap::IsPossible( CFFBot *me )
{
	if ( !me || me->GetClassSlot() != CLASS_DEMOMAN )
		return false;

	// Don't try a trap mid-fight — bot should retreat or attack instead.
	if ( me->GetTimeSinceLastInjury( TEAM_ANY ) < 1.0f )
		return false;

	// Must own pipelauncher with rounds.
	CBaseCombatWeapon *pl = me->Weapon_OwnsThisType( "ff_weapon_pipelauncher" );
	if ( !pl )
		return false;

	if ( pl->Clip1() <= 0 )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
CFFBotDemomanStickyTrap::CFFBotDemomanStickyTrap( void )
{
	m_targetArea = NULL;
	m_stickiesPlaced = 0;
	m_stickyTarget = 5;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDemomanStickyTrap::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_stickiesPlaced = 0;
	m_stickyTarget = RandomInt( FFBOT_MIN_STICKIES_IN_TRAP, FFBOT_MAX_STICKIES_IN_TRAP );

	CFFNavArea *myArea = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
	if ( !myArea )
		return Done( "No nav area" );

	// Hand-authored pipe-trap ground (FF_NAV2_PIPETRAP) outranks everything
	// below. Where to carpet is a different question from where to stand while
	// doing it, and the invasion-vector heuristic below only ever answers the
	// second one: it picks an adjacent area enemies come from, which is a
	// reasonable guess and nothing more. An author who marked a trap knows
	// which doorway the push actually uses.
	{
		const Vector myPos = me->GetAbsOrigin();
		CFFNavArea *authored = NULL;
		float bestDistSq = 1200.0f * 1200.0f;	// has to be somewhere we can shoot

		for ( int i = 0; i < TheNavAreas.Count(); ++i )
		{
			CFFNavArea *cand = static_cast< CFFNavArea * >( TheNavAreas[ i ] );
			if ( !cand->HasAttributeFF2( FF_NAV2_PIPETRAP ) )
				continue;
			if ( cand->HasAttributeFF( FF_NAV_UNDERWATER ) )
				continue;	// stickies don't work down there

			const float dSq = ( cand->GetCenter() - myPos ).LengthSqr();
			if ( dSq < bestDistSq )
			{
				bestDistSq = dSq;
				authored = cand;
			}
		}

		if ( authored )
		{
			m_targetArea = authored;

			CBaseCombatWeapon *authoredPl = me->Weapon_OwnsThisType( "ff_weapon_pipelauncher" );
			if ( authoredPl )
				me->Weapon_Switch( authoredPl );

			m_aimTimer.Start( 0.5f );
			return Continue();
		}
	}

	const CUtlVector< CFFNavArea * > &invasion =
		myArea->GetEnemyInvasionAreaVector( me->GetTeamNumber() );
	if ( invasion.Count() == 0 )
		return Done( "No invasion direction to trap" );

	// Prefer FF_NAV_CHOKE areas in the invasion vector. A choke is by
	// definition narrow — exactly where stickies do their best work
	// (enemy can't side-step the blast). Underwater is always rejected:
	// stickies don't detonate properly in water and the bubble trail
	// gives them away.
	CUtlVector< CFFNavArea * > chokes;
	CUtlVector< CFFNavArea * > viable;
	for ( int i = 0; i < invasion.Count(); ++i )
	{
		CFFNavArea *cand = invasion[ i ];
		if ( !cand )
			continue;
		const unsigned int attrs = cand->GetAttributesFF();
		if ( attrs & FF_NAV_UNDERWATER )
			continue;
		viable.AddToTail( cand );
		if ( attrs & FF_NAV_CHOKE )
			chokes.AddToTail( cand );
	}
	if ( chokes.Count() > 0 )
		m_targetArea = chokes[ RandomInt( 0, chokes.Count() - 1 ) ];
	else if ( viable.Count() > 0 )
		m_targetArea = viable[ RandomInt( 0, viable.Count() - 1 ) ];
	else
		return Done( "No viable trap target (all underwater)" );

	// Switch to pipelauncher.
	CBaseCombatWeapon *pl = me->Weapon_OwnsThisType( "ff_weapon_pipelauncher" );
	if ( pl )
		me->Weapon_Switch( pl );

	m_aimTimer.Start( 0.5f );
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDemomanStickyTrap::Update( CFFBot *me, float interval )
{
	if ( m_stickiesPlaced >= m_stickyTarget )
		return Done( "Trap laid" );

	if ( !m_targetArea )
		return Done( "Target area lost" );

	// Bail out if an enemy is bearing down on us.
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;
	if ( threat && threat->IsVisibleRecently() &&
		 me->IsRangeLessThan( threat->GetLastKnownPosition(), 500.0f ) )
	{
		return Done( "Enemy approaching — abort trap" );
	}

	// Pipelauncher must be active for fire to drop a sticky.
	CFFWeaponBase *active = me->GetActiveFFWeapon();
	if ( !active || !FStrEq( active->GetClassname(), "ff_weapon_pipelauncher" ) )
	{
		CBaseCombatWeapon *pl = me->Weapon_OwnsThisType( "ff_weapon_pipelauncher" );
		if ( pl )
			me->Weapon_Switch( pl );
		return Continue();
	}

	// Out of stickies — done with what we have.
	if ( active->Clip1() <= 0 )
		return Done( "Pipelauncher clip empty" );

	// Aim with a small per-shot offset so stickies spread across the
	// chokepoint rather than stacking on one tile.
	const Vector aimBase = m_targetArea->GetCenter() + Vector( 0, 0, 24.0f );
	const float spread = 64.0f;
	const Vector aimOffset(
		RandomFloat( -spread, spread ),
		RandomFloat( -spread, spread ),
		RandomFloat( 0.0f, 32.0f ) );
	const Vector aimAt = aimBase + aimOffset;

	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		body->AimHeadTowards( aimAt, IBody::CRITICAL, 0.3f, NULL,
			"Aiming sticky throw" );
	}

	// Wait for head to steady before each fire — we don't want stickies
	// flung wildly on the way to aim.
	if ( !body || body->IsHeadSteady() )
	{
		if ( m_fireGapTimer.IsElapsed() )
		{
			me->PressFireButton( 0.1f );
			++m_stickiesPlaced;
			m_fireGapTimer.Start( 0.4f );
		}
	}

	return Continue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotDemomanStickyTrap::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}
