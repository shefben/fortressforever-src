//========= Fortress Forever Bot =============================================//
//
// CFFBotAttack — chase-the-threat sub-action.
//
// Aim and fire are handled by CFFBotMainAction (per-tick). This action only
// drives positioning: chase the threat's last-known position when out of
// attack range or when LOS is lost. Returns DONE so MainAction's underlying
// Wander resumes once we're in range / no longer have a threat.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_attack.h"
#include "ff_bot_path_cost.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Range under which we stop chasing.
#define FFBOT_ATTACK_HOLD_RANGE	600.0f

// Give up if threat unseen this long.
#define FFBOT_LOSE_SIGHT_TIME	3.0f


CFFBotAttack::CFFBotAttack()
{
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotAttack::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_chasePath.SetMinLookAheadDistance( 300.0f );
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

	const CKnownEntity *threat = vision->GetPrimaryKnownThreat( false );
	if ( !threat || !threat->GetEntity() || threat->IsObsolete() )
	{
		return Done( "Threat lost" );
	}

	// Refresh lose-sight timer when we recently saw the threat.
	if ( threat->IsVisibleRecently() )
	{
		m_loseSightTimer.Start( FFBOT_LOSE_SIGHT_TIME );
	}
	else if ( m_loseSightTimer.IsElapsed() )
	{
		return Done( "Threat unseen too long" );
	}

	const Vector threatPos = threat->GetEntity()->GetAbsOrigin();
	const float distSq = ( threatPos - me->GetAbsOrigin() ).LengthSqr();

	if ( distSq <= FFBOT_ATTACK_HOLD_RANGE * FFBOT_ATTACK_HOLD_RANGE && threat->IsVisibleRecently() )
	{
		// Within attack range — stop chasing, let MainAction's per-tick
		// aim+fire do its thing while we hold position.
		m_chasePath.Invalidate();
		return Done( "In attack range" );
	}

	// Chase to last-known position.
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
		CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
		m_chasePath.Compute( me, threat->GetLastKnownPosition(), cost );
	}
	m_chasePath.Update( me );

	return Continue();
}
