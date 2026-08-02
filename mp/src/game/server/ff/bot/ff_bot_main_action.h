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

	// Every hit the bot takes, with the full damage info. This is the only
	// place the environmental-hazard detector can live: FF has no "you are
	// being gassed" signal, but the damage that gas does carries its type bits
	// and its (absent) attacker, and that is enough to tell it from a rocket.
	virtual EventDesiredResult< CFFBot > OnInjured( CFFBot *me, const CTakeDamageInfo &info ) OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "MainAction"; }

private:
	void UpdateLookingAroundForEnemies( CFFBot *me );	// per-tick aim driver
	void FireWeaponAtEnemy( CFFBot *me );				// per-tick fire driver
	void HandleRespawnInput( CFFBot *me );				// press fire while dead so we respawn
	void HandleStuckState( CFFBot *me );				// stuck escalation ladder (FIX 5)
	void HandleMobility( CFFBot *me );					// bunny-hop, crouch-jump, etc.

	// FIX 6 — doors are a first-class behavior, not an obstacle. Returns true
	// if a door interaction is in progress, in which case stuck recovery must
	// stand down (backing away would leave the door's trigger volume).
	bool HandleDoors( CFFBot *me );

	// Buttons are not doors. A func_button that opens a door twenty feet away
	// never appears as a blocker, so HandleDoors never sees it, and pressing
	// +use while walking into the door it controls does nothing at all. When
	// the bot is wedged and there is a button it can see, go and press that
	// instead. Returns true while working one.
	bool HandleButtons( CFFBot *me );

	// FIX 1 — the single movement authority. Runs last, after every other
	// per-tick concern has had its chance to publish a move override. Issues
	// at most one locomotor->Approach() per tick.
	void DriveMovementArbiter( CFFBot *me );

	// True while a teammate is directly in front of us and we should stand
	// still and let them clear. Mirrors PathFollower::FindBlocker, which does
	// the same thing for path-driven movement.
	bool ShouldYieldToBlocker( CFFBot *me );

	// bot_show_path / bot_show_threat overlays.
	void DrawDebugOverlays( CFFBot *me );
};

#endif // FF_BOT_MAIN_ACTION_H
