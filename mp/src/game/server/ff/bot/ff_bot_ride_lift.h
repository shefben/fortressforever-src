//========= Fortress Forever Bot =============================================//
//
// CFFBotRideLift — moving platforms, and the buttons that call them.
//
// A lift breaks two assumptions the path follower is built on:
//
//   * a nav area's position is constant. It isn't; the area sits in world space
//     where the platform was at level init, and the platform moves out from
//     under it.
//   * arriving somewhere is a matter of moving. On a lift it is a matter of
//     waiting, and every movement system in the bot treats "not moving" as a
//     symptom to be corrected. Left alone, the stuck ladder escalates while the
//     bot is riding, and stage 2 back-tracks it straight off the platform.
//
// FoxBot carried a per-waypoint W_FL_LIFT flag for exactly this reason, with a
// tightened arrival tolerance and the comment "some lifts are small (e.g.
// rock2's lifts)". FF_NAV2_LIFT is the same flag; CFFBotAutoTagger derives it
// from func_train / func_plat / func_tracktrain / a vertically-travelling
// func_door, and ff_nav_place lift covers the ones that are none of those.
//
// The button half matters more than the lift half. Source buttons are used, not
// walked into: CBasePlayer::PlayerUse traces from the eye and requires the
// player to be close and looking at the thing. A bot that only ever presses
// +use while walking forward into geometry will never press one. Any map with a
// button-called elevator, or a button-opened door away from the door itself, is
// a hard stop for a bot that cannot do this — and "hard stop" means the bot
// stands in the corridor until the round ends.
//
//===========================================================================//

#ifndef FF_BOT_RIDE_LIFT_H
#define FF_BOT_RIDE_LIFT_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFNavArea;


namespace FFBotLift
{
	// True for brush entities that carry players vertically.
	//
	// func_door and func_movelinear qualify only when their move direction is
	// mostly vertical: a horizontal one is a door, a vertical one is an
	// elevator wearing a door's classname, and FF maps use both spellings.
	//
	// Shared with CFFBotAutoTagger, which stamps FF_NAV2_LIFT from the same
	// test — one definition so the tag and the behaviour can't disagree.
	bool IsLiftEntity( CBaseEntity *ent );

	// The lift platform currently under `pos`, or NULL. Traced rather than
	// looked up, because which platform is at a given place is a question about
	// right now and the nav area only records where one was at level init.
	CBaseEntity *PlatformUnder( const Vector &pos, float searchDown = 128.0f );

	// Nearest pressable button within `radius` of `pos` that the bot can see.
	// Covers func_button, func_rot_button and momentary_rot_button.
	CBaseEntity *FindButtonNear( CFFBot *me, const Vector &pos, float radius );

	// Walk-up-and-press. Publishes a move override towards the button when out
	// of range, aims the head at it, and presses use when close enough for
	// CBasePlayer::PlayerUse to find it.
	//
	// Returns true while the bot is working the button, which is the caller's
	// cue that stuck recovery must stand down: pressing a button legitimately
	// involves standing still in front of a wall.
	bool WorkButton( CFFBot *me, CBaseEntity *button );
}


class CFFBotRideLift : public Action< CFFBot >
{
public:
	CFFBotRideLift( void );

	// True when the bot's route wants a lift area it is standing on or next to.
	static bool IsPossible( CFFBot *me );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "RideLift"; }

private:
	enum State
	{
		// The platform isn't here. Find the button that calls it and press it.
		STATE_CALL,

		// The platform is here and we aren't on it. Walk on.
		STATE_BOARD,

		// We're on it. Stand still and let it work — this is the state every
		// other movement system in the bot would otherwise try to "fix".
		STATE_RIDE,
	};

	State  m_state;
	Vector m_liftPos;			// centre of the lift area we're using
	float  m_boardZ;			// our Z when we stepped on
	float  m_lastPlatformZ;
	float  m_platformStillSince;

	EHANDLE m_button;
	CountdownTimer m_giveUpTimer;
	CountdownTimer m_buttonRetryTimer;
	CountdownTimer m_noButtonTimer;	// shaft with no visible call button
};


#endif // FF_BOT_RIDE_LIFT_H
