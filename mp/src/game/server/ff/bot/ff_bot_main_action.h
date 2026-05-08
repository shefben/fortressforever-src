//========= Fortress Forever Bot =============================================//
//
// CFFBotMainAction — top-level Action.
//
// In addition to dispatching sub-actions (Wander / Attack / future CTF / etc.),
// MainAction is the per-tick driver for *continuous* concerns: head aiming,
// weapon firing, weapon selection. This matches TFBot's architecture and is
// necessary because PlayerLocomotion::FaceTowards calls AimHeadTowards with
// BORING priority every tick — only a same-priority-or-higher refresh from
// MainAction keeps the bot looking at threats while it moves.
//
// Sub-actions handle movement/strategy ONLY; they do not aim or fire.
//
//===========================================================================//

#ifndef FF_BOT_MAIN_ACTION_H
#define FF_BOT_MAIN_ACTION_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "ff_bot.h"

class CFFBotMainAction : public Action< CFFBot >
{
public:
	CFFBotMainAction( void ) {}

	virtual Action< CFFBot > *		InitialContainedAction( CFFBot *me ) OVERRIDE;
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval ) OVERRIDE;

	// While stuck on a door / obstacle, mash USE + JUMP. The active sub-action
	// (Wander) handles the path-replan side via its own OnStuck.
	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me ) OVERRIDE;

	// Hearing: when an in-game sound originates near us (footsteps, gunfire,
	// grenade explosions), record the position as a recent enemy hint and
	// broadcast it to teammates as a team alert. Comes through the NextBot
	// event-responder cascade.
	virtual EventDesiredResult< CFFBot > OnSound( CFFBot *me, CBaseEntity *source, const Vector &pos, KeyValues *keys ) OVERRIDE;

	// On death, hand off to CFFBotDead so the entire MainAction tree is
	// torn down. On respawn, CFFBotDead transitions back to a fresh
	// CFFBotMainAction (mirrors TFBot's CTFBotMainAction::OnKilled).
	// Without this, behaviors persist stale state across deaths.
	virtual EventDesiredResult< CFFBot > OnKilled( CFFBot *me, const CTakeDamageInfo &info ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "MainAction"; }

private:
	void UpdateLookingAroundForEnemies( CFFBot *me );	// per-tick aim driver
	void FireWeaponAtEnemy( CFFBot *me );				// per-tick fire driver
	void HandleRespawnInput( CFFBot *me );				// press fire while dead so we respawn
	void HandleStuckState( CFFBot *me );				// per-tick mash through doors / obstacles
	void HandleMobility( CFFBot *me );					// bunny-hop, crouch-jump, etc.
};

#endif // FF_BOT_MAIN_ACTION_H
