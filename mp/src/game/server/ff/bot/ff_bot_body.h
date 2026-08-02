//========= Fortress Forever Bot =============================================//
//
// CFFBotBody — PlayerBody subclass, head-aim parameters.
//
// WHAT CHANGED AND WHY IT WAS WRONG.
//
// This used to override GetMaxHeadAngularVelocity to 3000 deg/s, three times
// Valve's nb_saccade_speed default of 1000, with the reasoning "snappy combat
// aim". Two things are wrong with that, and the second one is not about aim at
// all.
//
// First, it defeats the entire model. PlayerBody::Update turns the head with
// ApproachAngle at a rate that eases in over the first 0.25s and eases out as
// the aim converges, specifically so the aim does not pop:
//
//     if ( dot > 0.7f ) approachRate *= sin( 1.57f * RemapVal( dot, 0.7, 1, 1, 0.02 ) );
//
// At 3000 deg/s the bot covers 45 degrees in a single 66Hz tick, so it reaches
// any target within a tick or two no matter what the easing says. Every one of
// those curves is multiplied by a rate large enough to make it irrelevant. The
// bot does not aim, it snaps — and IsHeadSteady, which asks whether the head is
// turning slower than nb_head_aim_steady_max_rate (100 deg/s), becomes a test
// that is either trivially true or trivially false with nothing in between.
//
// Second, and this is the part that matters more: IN SOURCE, THE HEAD IS THE
// MOVEMENT BASIS. PlayerLocomotion::Approach decomposes (goal - feet) into
// forward and side button presses in VIEW space. So a head that snaps 90
// degrees snaps the meaning of "forward" with it. The bot was walking down a
// corridor; a threat appears; the view jumps; and for the next tick or two the
// button it is holding points at the wall. That is a mechanical cause of bots
// running into walls and corners, and rate-limiting the head is the fix.
//
// TF does not override this value at all. Neither do we now.
//
//===========================================================================//

#ifndef FF_BOT_BODY_H
#define FF_BOT_BODY_H
#ifdef _WIN32
#pragma once
#endif

#include "Player/NextBotPlayerBody.h"

extern ConVar ff_bot_difficulty;

class CFFBotBody : public PlayerBody
{
public:
	CFFBotBody( INextBot *bot ) : PlayerBody( bot ) {}

	// Falls through to nb_saccade_speed (1000 deg/s), which is tunable live
	// and shared with every other NextBot in the build. If FF genuinely wants
	// faster aim than TF, that cvar is the honest place to say so — raising it
	// there at least keeps the easing meaningful, because the ease-out is a
	// fraction of whatever the rate is.
	//
	// virtual float GetMaxHeadAngularVelocity( void ) const OVERRIDE;

	// How often we resample the target's position and velocity.
	//
	// This is TF's entire body override — CTFBotBody is 43 lines and this is
	// the only thing in it — and it is where most of the FELT difficulty
	// difference comes from. Between samples the bot aims at a stale
	// prediction, so a bot on a one-second interval genuinely cannot track a
	// strafing scout, and one on 0.05s effectively never loses it. That reads
	// as skill rather than as a handicap, which is exactly what you want.
	//
	// Values are Valve's, mapped onto ff_bot_difficulty (0 easy .. 3 expert).
	virtual float GetHeadAimTrackingInterval( void ) const OVERRIDE
	{
		switch ( ff_bot_difficulty.GetInt() )
		{
		case 0:  return 1.0f;	// easy
		case 1:  return 0.25f;	// normal
		case 2:  return 0.1f;	// hard
		default: return 0.05f;	// expert
		}
	}

	// We compute projectile-aware leading manually per-tick in
	// CFFBotMainAction (CFFWeaponBase + distance + target velocity ⇒ time-of-
	// flight ⇒ lead position). Disable the body's built-in subject lead so
	// it doesn't double-count.
	virtual float GetHeadAimSubjectLeadTime( void ) const OVERRIDE { return 0.0f; }
};

#endif // FF_BOT_BODY_H
