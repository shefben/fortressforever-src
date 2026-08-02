//========= Fortress Forever Bot =============================================//
//
// CFFBotAttack — see header.
//
// Ported from hl2_src tf_bot_attack with FF adaptations:
//   - Skip TFBot's circle-strafe inside this action; FF's CFFBotMainAction
//     already runs HandleCombatStrafe per-tick at the parent level, so
//     duplicating it here would fight that.
//   - FFBot keeps a lose-sight timer (gives up if threat unseen too long)
//     to handle FF's faster pace where a missed target should not pin the
//     bot indefinitely chasing its ghost.
//   - Uses CFFBotPathCost FFBOT_FASTEST_ROUTE since CFFBotPathCost does not
//     yet have a SAFEST_ROUTE distinct from FASTEST.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_attack.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_nav_area.h"
#include "nav_mesh.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define FFBOT_LOSE_SIGHT_TIME 3.0f


//-----------------------------------------------------------------------------
CFFBotAttack::CFFBotAttack() : m_chasePath( ChasePath::LEAD_SUBJECT )
{
	m_inCoverPhase = false;
	m_coverPos.Init();
}


//-----------------------------------------------------------------------------
// Find a nav area near `me` whose center has LOS to me but NOT to threat —
// i.e., somewhere we can stand to be hidden from threat while remaining
// reachable. Returns vec3_origin on failure (no usable cover nearby).
//
// Searches the bot's surrounding nav areas (~512u). Doesn't use BFS / travel
// distance because we want the cycle to be tight; a sub-second peek/cover
// loop can't afford a full BFS each tick. Random walk over nearby areas is
// good enough — even an imperfect cover is better than standing in the open.
//-----------------------------------------------------------------------------
static Vector FindNearbyCoverFrom( CFFBot *me, const Vector &threatEyePos )
{
	const Vector myPos = me->GetAbsOrigin();
	CNavArea *here = me->GetLastKnownArea();
	if ( !here )
		return vec3_origin;

	const float searchSq = 512.0f * 512.0f;
	Vector bestCover = vec3_origin;
	float bestDistSq = FLT_MAX;	// prefer closest valid cover

	for ( int i = 0; i < TheNavAreas.Count(); ++i )
	{
		CNavArea *area = TheNavAreas[ i ];
		if ( !area )
			continue;
		const Vector spotPos = area->GetCenter();
		const float dSq = ( spotPos - myPos ).LengthSqr();
		if ( dSq > searchSq )
			continue;
		if ( dSq < ( 80.0f * 80.0f ) )
			continue;	// "right where we are" doesn't count as cover

		// Cover means: traceline from threat eye to this spot eye is blocked
		// by world geometry.
		const Vector spotEye = spotPos + Vector( 0, 0, 64.0f );
		trace_t tr;
		UTIL_TraceLine( threatEyePos, spotEye, MASK_SHOT, NULL,
			COLLISION_GROUP_NONE, &tr );
		if ( tr.fraction >= 0.99f )
			continue;	// threat can see this spot — not cover

		if ( dSq < bestDistSq )
		{
			bestDistSq = dSq;
			bestCover = spotPos;
		}
	}

	return bestCover;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotAttack::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_coverPath.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_repathTimer.Invalidate();
	m_loseSightTimer.Start( FFBOT_LOSE_SIGHT_TIME );
	m_inCoverPhase = false;
	m_coverPos.Init();
	m_coverPhaseTimer.Invalidate();
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotAttack::Update( CFFBot *me, float interval )
{
	IVision *vision = me->GetVisionInterface();
	if ( !vision )
		return Done( "No vision interface" );

	const CKnownEntity *threat = vision->GetPrimaryKnownThreat();
	if ( threat == NULL || threat->IsObsolete() || !threat->GetEntity() )
	{
		return Done( "No threat" );
	}

	// Refresh weapon selection — range may have changed since we started.
	me->EquipBestWeaponForThreat( threat );

	// Lose-sight watchdog (FF addition). Refresh while we still see them;
	// give up if too long without sight.
	if ( threat->IsVisibleRecently() )
	{
		m_loseSightTimer.Start( FFBOT_LOSE_SIGHT_TIME );
	}
	else if ( m_loseSightTimer.IsElapsed() )
	{
		return Done( "Threat unseen too long" );
	}

	// In attack range, visible, with clear LOS — let MainAction's per-tick
	// aim+fire engage. We've done our job (closed the gap).
	//
	// (An earlier revision ran a cover-peek state machine here that put the
	// bot in cover for 1.5-2.5s of every 3s cycle. It worked in isolation
	// but combined with the reaction-time gate's per-threat reset and the
	// per-tick aim error re-roll it left the bot effectively never aiming
	// AND never firing. Reverted to the original "Done in range" behavior
	// to restore baseline. Cover-peek as a separate sub-action is the right
	// shape and can be re-introduced once we have the right hooks for
	// suspending/resuming it without fighting the parent's path.)
	if ( threat->IsVisibleRecently() &&
		 me->IsRangeLessThan( threat->GetEntity()->GetAbsOrigin(), me->GetDesiredAttackRange() ) &&
		 me->IsLineOfFireClear( threat->GetEntity()->EyePosition() ) )
	{
		m_chasePath.Invalidate();
		return Done( "In attack range" );
	}

	// Pursue the threat.
	if ( threat->IsVisibleRecently() )
	{
		// Visible — chase with subject leading.
		CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
		m_chasePath.Update( me, threat->GetEntity(), cost );
	}
	else
	{
		// Not visible — head toward last-known position.
		m_chasePath.Invalidate();

		const Vector lkp = threat->GetLastKnownPosition();
		if ( me->IsRangeLessThan( lkp, 20.0f ) )
		{
			// Reached LKP and they're still not visible — give up on this
			// threat so the bot doesn't pin itself searching forever.
			vision->ForgetEntity( threat->GetEntity() );
			return Done( "Lost target at LKP" );
		}

		// Look toward LKP while approaching, but only if within attack range.
		if ( me->IsRangeLessThan( lkp, me->GetMaxAttackRange() ) )
		{
			IBody *body = me->GetBodyInterface();
			if ( body )
			{
				body->AimHeadTowards( lkp + Vector( 0, 0, 50.0f ),
					IBody::IMPORTANT, 0.2f, NULL, "Toward last-known position" );
			}
		}

		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );

		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			// FIX 7 — repath hysteresis: leave a working path alone while the bot
			// is actually travelling it. Volatile cost terms (combat intensity,
			// grenade danger, ammo/health discounts, recent-stuck penalties) used
			// to flip A* between near-equal lanes on consecutive repaths.
			if ( FFBotHelpers::ShouldRecomputePath( me, m_path, lkp ) )
			{
				CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
				m_path.Compute( me, lkp, cost );
			}
		}
	}

	return Continue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotAttack::OnStuck( CFFBot *me )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotAttack::OnMoveToSuccess( CFFBot *me, const Path *path )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotAttack::OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason )
{
	return TryContinue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotAttack::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_UNDEFINED;
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotAttack::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_UNDEFINED;
}
