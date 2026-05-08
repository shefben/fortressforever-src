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
#include "ff_bot_path_cost.h"
#include "NextBotKnownEntity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define FFBOT_LOSE_SIGHT_TIME 3.0f


//-----------------------------------------------------------------------------
CFFBotAttack::CFFBotAttack() : m_chasePath( ChasePath::LEAD_SUBJECT )
{
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotAttack::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	m_repathTimer.Invalidate();
	m_loseSightTimer.Start( FFBOT_LOSE_SIGHT_TIME );
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

		m_path.Update( me );

		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			m_path.Compute( me, lkp, cost );
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
