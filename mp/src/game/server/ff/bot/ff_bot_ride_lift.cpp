//========= Fortress Forever Bot =============================================//
//
// CFFBotRideLift / FFBotLift — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_ride_lift.h"
#include "ff_bot_helpers.h"
#include "ff_nav_area.h"
#include "ff_nav_mesh.h"
#include "ff_player.h"
#include "entitylist.h"
#include "nav_mesh.h"
#include "NextBotBodyInterface.h"
#include "NextBotLocomotionInterface.h"
#include "engine/IEngineTrace.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_use_lifts( "ff_bot_use_lifts", "1", FCVAR_NONE,
	"Ride moving platforms and press the buttons that call them. "
	"0 = off, 1 = on, 2 = on and log every transition." );


// How close CBasePlayer::PlayerUse needs us to be. The engine's own radius is
// PLAYER_USE_RADIUS (80); we aim for comfortably inside it, because the trace
// starts at the eye and a bot standing at exactly 80u with a slightly-off head
// angle misses.
#define FFBOT_BUTTON_PRESS_RANGE		56.0f

// Beyond this we don't consider a button related to the lift at all.
#define FFBOT_LIFT_BUTTON_SEARCH		400.0f

// A platform that has moved less than this in a tick counts as stopped.
#define FFBOT_LIFT_STILL_EPSILON		1.0f

// ...for this long. Long enough to span the pause at the top of a two-stage
// lift without ending the ride, short enough not to strand the bot.
#define FFBOT_LIFT_STILL_TIME			0.75f

// Vertical travel that counts as "the lift did something".
#define FFBOT_LIFT_MIN_TRAVEL			48.0f

// Whole-action timeouts. A lift the bot cannot make work must not hold it
// forever; the objective action will pick a different route.
#define FFBOT_LIFT_TIMEOUT				30.0f
#define FFBOT_LIFT_BUTTON_RETRY			3.0f

// How long to stand in a shaft hoping a platform turns up when there is no
// button we can see to call it with.
#define FFBOT_LIFT_NO_BUTTON_WAIT		6.0f

// How far ahead along the path we look for a lift area.
#define FFBOT_LIFT_LOOKAHEAD			256.0f


//=============================================================================
// FFBotLift helpers.
//=============================================================================

bool FFBotLift::IsLiftEntity( CBaseEntity *ent )
{
	if ( !ent )
		return false;

	if ( FClassnameIs( ent, "func_train" ) ||
	     FClassnameIs( ent, "func_plat" ) ||
	     FClassnameIs( ent, "func_platrot" ) ||
	     FClassnameIs( ent, "func_tracktrain" ) ||
	     FClassnameIs( ent, "func_elevator" ) )
	{
		return true;
	}

	if ( FClassnameIs( ent, "func_door" ) || FClassnameIs( ent, "func_movelinear" ) )
	{
		// Movement direction lives in the entity's angles as a move dir. A
		// steeply-inclined one carries players; a flat one just opens.
		Vector moveDir;
		AngleVectors( ent->GetLocalAngles(), &moveDir );
		return ( fabsf( moveDir.z ) > 0.7f );
	}

	return false;
}


//-----------------------------------------------------------------------------
CBaseEntity *FFBotLift::PlatformUnder( const Vector &pos, float searchDown )
{
	trace_t tr;
	const Vector start = pos + Vector( 0.0f, 0.0f, 8.0f );
	const Vector end   = pos - Vector( 0.0f, 0.0f, searchDown );

	UTIL_TraceLine( start, end, MASK_PLAYERSOLID, NULL, COLLISION_GROUP_PLAYER_MOVEMENT, &tr );

	if ( tr.fraction >= 1.0f || !tr.m_pEnt )
		return NULL;

	return IsLiftEntity( tr.m_pEnt ) ? tr.m_pEnt : NULL;
}


//-----------------------------------------------------------------------------
CBaseEntity *FFBotLift::FindButtonNear( CFFBot *me, const Vector &pos, float radius )
{
	static const char * const kButtonClasses[] = {
		"func_button",
		"func_rot_button",
		"momentary_rot_button",
	};

	const float radiusSq = radius * radius;

	CBaseEntity *best = NULL;
	float bestDistSq = FLT_MAX;

	for ( int c = 0; c < (int)ARRAYSIZE( kButtonClasses ); ++c )
	{
		CBaseEntity *ent = NULL;
		while ( ( ent = gEntList.FindEntityByClassname( ent, kButtonClasses[ c ] ) ) != NULL )
		{
			// A button with no draw is either already pressed-and-held or was
			// removed by the map's logic. Either way we can't work it.
			if ( ent->IsEffectActive( EF_NODRAW ) )
				continue;

			const Vector center = ent->WorldSpaceCenter();
			const float dSq = ( center - pos ).LengthSqr();
			if ( dSq > radiusSq || dSq >= bestDistSq )
				continue;

			// Has to be something we could actually walk up to and see. The
			// trace is from the bot's eye rather than from `pos`, because the
			// bot is the one that has to press it.
			if ( me && !me->IsLineOfFireClear( center ) )
				continue;

			bestDistSq = dSq;
			best = ent;
		}
	}

	return best;
}


//-----------------------------------------------------------------------------
bool FFBotLift::WorkButton( CFFBot *me, CBaseEntity *button )
{
	if ( !me || !button )
		return false;

	const Vector target = button->WorldSpaceCenter();

	// Look at it. Source's PlayerUse traces along the eye vector, so aiming is
	// not cosmetic here — it is the mechanism.
	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		body->AimHeadTowards( target, IBody::IMPORTANT, 0.3f, NULL, "Pressing a button" );
	}

	const float distSq = ( target - me->EyePosition() ).LengthSqr();

	if ( distSq > ( FFBOT_BUTTON_PRESS_RANGE * FFBOT_BUTTON_PRESS_RANGE ) )
	{
		// Close the distance through the arbiter rather than with a raw
		// IN_FORWARD: forward is view-relative, and we have just pointed the
		// view at the button, which is usually in a wall.
		me->SetMoveOverride( target, 0.2f, "Walking to a button" );
		return true;
	}

	me->PressUseButton( 0.3f );

	// Stand still while pressing. Without this the path follower keeps walking
	// and the bot drifts out of use range between ticks.
	me->SetMoveOverride( me->GetAbsOrigin(), 0.2f, "Pressing a button" );

	// Working a button is progress, not being stuck.
	me->m_lastUnstuckTime = gpGlobals->curtime;
	me->m_stuckStage = 0;
	me->m_stuckStageTime = gpGlobals->curtime;
	return true;
}


//=============================================================================
// CFFBotRideLift.
//=============================================================================

CFFBotRideLift::CFFBotRideLift( void )
{
	m_state = STATE_CALL;
	m_liftPos.Init();
	m_boardZ = 0.0f;
	m_lastPlatformZ = 0.0f;
	m_platformStillSince = 0.0f;
}


//-----------------------------------------------------------------------------
// The lift area our route wants, if any.
//
// Two ways to find one, in order of confidence: the area we are standing in,
// and an area close ahead along the path. The second is what stops the bot
// walking up to a shaft and standing in front of it, which is what happens when
// only the first is checked and the platform is parked at another floor.
//-----------------------------------------------------------------------------
static CFFNavArea *FindRouteLiftArea( CFFBot *me )
{
	CFFNavArea *here = static_cast< CFFNavArea * >( me->GetLastKnownArea() );
	if ( here && here->HasAttributeFF2( FF_NAV2_LIFT ) )
		return here;

	Vector goal;
	if ( !me->GetPathGoal( &goal ) )
		return NULL;

	const Vector myPos = me->GetAbsOrigin();
	if ( ( goal - myPos ).LengthSqr() > ( FFBOT_LIFT_LOOKAHEAD * FFBOT_LIFT_LOOKAHEAD ) )
		return NULL;

	if ( !TheNavMesh )
		return NULL;

	CNavArea *ahead = TheNavMesh->GetNearestNavArea( goal );
	if ( !ahead )
		return NULL;

	CFFNavArea *ffAhead = static_cast< CFFNavArea * >( ahead );
	return ffAhead->HasAttributeFF2( FF_NAV2_LIFT ) ? ffAhead : NULL;
}


//-----------------------------------------------------------------------------
bool CFFBotRideLift::IsPossible( CFFBot *me )
{
	if ( ff_bot_use_lifts.GetInt() <= 0 )
		return false;
	if ( !me || !me->IsAlive() )
		return false;

	// A bot on a ladder is committed to the ladder; the locomotor owns it.
	ILocomotion *loco = me->GetLocomotionInterface();
	if ( loco && ( loco->IsUsingLadder() || loco->IsAscendingOrDescendingLadder() ) )
		return false;

	CFFNavArea *lift = FindRouteLiftArea( me );
	if ( !lift )
		return false;

	// Having a lift on the route is not the same as needing it. FF_NAV2_LIFT is
	// stamped from a brush entity's bounding box, so it lands on plenty of
	// areas next to a lift as well as on the platform itself, and a bot that
	// suspended into this action every time it walked past a shaft would spend
	// the round doing that.
	//
	// Two things make it a real lift interaction: the platform isn't here (so
	// somebody has to call it), or it is here and the route wants a different
	// floor.
	const Vector myPos = me->GetAbsOrigin();

	if ( !FFBotLift::PlatformUnder( lift->GetCenter() ) )
		return true;

	Vector goal;
	if ( !me->GetPathGoal( &goal ) )
		return false;

	return fabsf( goal.z - myPos.z ) > 64.0f;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotRideLift::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	CFFNavArea *lift = FindRouteLiftArea( me );
	if ( !lift )
		return Done( "No lift on the route after all" );

	m_liftPos = lift->GetCenter();
	m_boardZ = me->GetAbsOrigin().z;
	m_lastPlatformZ = m_liftPos.z;
	m_platformStillSince = gpGlobals->curtime;
	m_giveUpTimer.Start( FFBOT_LIFT_TIMEOUT );
	m_buttonRetryTimer.Invalidate();
	m_noButtonTimer.Invalidate();

	CBaseEntity *platform = FFBotLift::PlatformUnder( me->GetAbsOrigin() );
	if ( platform )
	{
		m_state = STATE_RIDE;
	}
	else if ( FFBotLift::PlatformUnder( m_liftPos ) )
	{
		m_state = STATE_BOARD;
	}
	else
	{
		m_state = STATE_CALL;
	}

	if ( ff_bot_use_lifts.GetInt() >= 2 )
	{
		Msg( "[FFBotLift] %s starts at lift (%.0f %.0f %.0f) in state %d\n",
			me->GetPlayerName(), m_liftPos.x, m_liftPos.y, m_liftPos.z, (int)m_state );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotRideLift::Update( CFFBot *me, float interval )
{
	if ( !me->IsAlive() )
		return Done( "Dead" );

	if ( m_giveUpTimer.IsElapsed() )
		return Done( "Lift took too long" );

	CBaseEntity *platformHere = FFBotLift::PlatformUnder( me->GetAbsOrigin() );

	switch ( m_state )
	{
	case STATE_CALL:
		{
			// Platform arrived on its own — somebody else called it, or it runs
			// on a timer.
			if ( platformHere || FFBotLift::PlatformUnder( m_liftPos ) )
			{
				m_state = platformHere ? STATE_RIDE : STATE_BOARD;
				if ( platformHere )
					m_boardZ = me->GetAbsOrigin().z;
				return Continue();
			}

			CBaseEntity *button = m_button.Get();
			if ( !button || m_buttonRetryTimer.IsElapsed() )
			{
				button = FFBotLift::FindButtonNear( me, m_liftPos, FFBOT_LIFT_BUTTON_SEARCH );
				m_button = button;
				m_buttonRetryTimer.Start( FFBOT_LIFT_BUTTON_RETRY );
			}

			if ( !button )
			{
				// No button we can see. The lift may be triggered by walking
				// into it, so give the shaft itself a short try — but a short
				// one. Standing in a shaft waiting for a platform that is never
				// coming is exactly as bad as the behaviour this action exists
				// to fix, and the objective layer has a route to re-plan.
				if ( m_noButtonTimer.HasStarted() && m_noButtonTimer.IsElapsed() )
					return Done( "No way to call this lift" );
				if ( !m_noButtonTimer.HasStarted() )
					m_noButtonTimer.Start( FFBOT_LIFT_NO_BUTTON_WAIT );

				me->SetMoveOverride( m_liftPos, 0.2f, "Waiting at a lift" );
				me->PressUseButton( 0.2f );
				return Continue();
			}

			m_noButtonTimer.Invalidate();

			FFBotLift::WorkButton( me, button );
			return Continue();
		}

	case STATE_BOARD:
		{
			if ( platformHere )
			{
				m_state = STATE_RIDE;
				m_boardZ = me->GetAbsOrigin().z;
				m_lastPlatformZ = platformHere->GetAbsOrigin().z;
				m_platformStillSince = gpGlobals->curtime;
				return Continue();
			}

			// The platform left before we got on. Back to calling it.
			if ( !FFBotLift::PlatformUnder( m_liftPos ) )
			{
				m_state = STATE_CALL;
				return Continue();
			}

			me->SetMoveOverride( m_liftPos, 0.2f, "Boarding a lift" );
			return Continue();
		}

	case STATE_RIDE:
		{
			if ( !platformHere )
			{
				// We're off it. Either it arrived and we stepped off, or it
				// left without us; both mean this action is finished and the
				// objective action should re-path from wherever we now are.
				return Done( "Off the platform" );
			}

			// Hold position. This is the entire point of the state: every other
			// movement system in the bot reads "not moving" as a fault, and the
			// stuck ladder's stage-2 back-track would walk us off the edge.
			me->SetMoveOverride( me->GetAbsOrigin(), 0.2f, "Riding a lift" );
			me->m_lastUnstuckTime = gpGlobals->curtime;
			me->m_stuckStage = 0;
			me->m_stuckStageTime = gpGlobals->curtime;

			const float platformZ = platformHere->GetAbsOrigin().z;
			if ( fabsf( platformZ - m_lastPlatformZ ) > FFBOT_LIFT_STILL_EPSILON )
			{
				m_platformStillSince = gpGlobals->curtime;
				m_lastPlatformZ = platformZ;
				return Continue();
			}

			// Stopped. If it actually took us somewhere, we're done; if it
			// never moved, we're standing on a parked platform and should stop
			// pretending we're riding it.
			if ( ( gpGlobals->curtime - m_platformStillSince ) >= FFBOT_LIFT_STILL_TIME )
			{
				const float travelled = fabsf( me->GetAbsOrigin().z - m_boardZ );
				if ( travelled >= FFBOT_LIFT_MIN_TRAVEL )
					return Done( "Lift arrived" );

				// Parked and not going anywhere. Try to call it once more, then
				// the give-up timer ends this.
				m_state = STATE_CALL;
				m_buttonRetryTimer.Invalidate();
			}

			return Continue();
		}
	}

	return Done( "Unreachable" );
}


//-----------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotRideLift::OnStuck( CFFBot *me )
{
	// Being "stuck" at a lift is the normal case: in RIDE we are deliberately
	// standing still, and in CALL we are standing in front of a button, which
	// is a wall. Swallow the event in every state so the escalation ladder
	// doesn't start unpicking a working interaction.
	return TryContinue();
}
