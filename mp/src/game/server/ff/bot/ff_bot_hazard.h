//========= Fortress Forever Bot =============================================//
//
// FFBotHazard / CFFBotEscapeHazard — environmental damage response.
//
// THE PROBLEM
//
// rock2 fills with gas partway through the round. There is a suit that stops it
// killing you, and every human on the server knows to go and get one. FoxBot's
// bots never did: TFC's suit was an undifferentiated item_tfgoal, and its damage
// handler put DMG_NERVEGAS on the ignore list, so its bots stood in the gas
// until they died. The same is true of lava pits, electrified floors, crushers
// and every other environmental killer on every other map.
//
// Two of the three hard parts were already solved elsewhere:
//
//   * WHAT to run to. FFBotLuaObjectives classifies the suit by model, which no
//     amount of map scripting could express — Omnibot's nine goal types have no
//     way to say "protective equipment". FF gives the suit its own model, so it
//     is identifiable where TFC's was not.
//   * WHERE the danger is. CFFBotAutoTagger stamps FF_NAV2_HAZARD_ZONE on every
//     nav area overlapping a trigger_hurt. The damage may be switched on and off
//     by Lua on a schedule we cannot read, but the volume is a real entity
//     present from level load.
//
// What was missing is WHEN. There is no engine-level "the map is filling with
// gas" signal, and there is no way to read the Lua schedule that turns it on.
//
// THE ANSWER
//
// Ask the damage. INextBotEventResponder::OnInjured carries the full
// CTakeDamageInfo, including the damage-type bits and the attacker, and it fires
// for environmental damage exactly as it does for a rocket. Damage that carries
// an environmental bit, or that arrives with no player behind it while we are
// standing in a tagged hazard volume, is the hazard being live. That needs no
// schedule and no map knowledge, and it works for a hazard nobody anticipated.
//
// While the signal is live:
//   * hazard volumes become expensive to path through (CFFBotPathCost), so the
//     route out avoids more gas rather than crossing the room diagonally;
//   * the bot suspends what it was doing and either fetches the gear or, if the
//     map has none, leaves the volume.
//
//===========================================================================//

#ifndef FF_BOT_HAZARD_H
#define FF_BOT_HAZARD_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CTakeDamageInfo;


namespace FFBotHazard
{
	// Called from CFFBotMainAction::OnInjured for every hit the bot takes.
	// Classifies it and, when it looks environmental, starts the clock.
	void OnInjured( CFFBot *me, const CTakeDamageInfo &info );

	// True while the bot has taken environmental damage recently enough that
	// it should still be treated as being in a live hazard. Read by the path
	// cost model and by the escape action.
	bool IsSuffering( CFFBot *me );

	// How long the bot has been taking environmental damage without a break.
	// Used to tell "I clipped the corner of a trigger_hurt" apart from "the
	// room is full of gas".
	float GetSufferingDuration( CFFBot *me );

	// Clear the clock. Called on respawn — the last life's gas is not this
	// life's problem.
	void Reset( CFFBot *me );

	// Nearest protective equipment this bot may pick up. Prefers a live
	// registry entity; falls back to nav areas tagged FF_NAV2_HAZARD_GEAR for
	// maps whose gear is not a script entity we can classify, which is why the
	// position and the entity are separate outputs — the marker case has a
	// place to go and nothing to hold a handle on.
	//
	// Returns false when there is no gear within reach at all.
	bool FindHazardGear( CFFBot *me, Vector *outPos, CBaseEntity **outEnt );

	// Nearest nav area not tagged FF_NAV2_HAZARD_ZONE. The answer to "there is
	// no suit on this map, get out".
	bool FindWayOut( CFFBot *me, Vector *outPos );
}


//-----------------------------------------------------------------------------
// Suspend-for action: fetch the gear, or failing that leave the volume.
//
// Deliberately one action rather than two. From the bot's point of view these
// are the same decision made against different map contents, and splitting them
// would mean two IsPossible scans per tick to answer one question.
//-----------------------------------------------------------------------------
class CFFBotEscapeHazard : public Action< CFFBot >
{
public:
	CFFBotEscapeHazard( void );

	// True when the bot is taking environmental damage and there is somewhere
	// useful to go about it.
	static bool IsPossible( CFFBot *me );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason ) OVERRIDE;

	// Getting out of a hazard is always urgent — this is what tells the
	// locomotor not to walk.
	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE { return ANSWER_YES; }

	virtual const char *GetName( void ) const OVERRIDE { return "EscapeHazard"; }

private:
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_giveUpTimer;

	Vector  m_goalPos;
	EHANDLE m_gear;
	bool    m_isFetchingGear;
};


#endif // FF_BOT_HAZARD_H
