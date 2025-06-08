//========= Fortress Forever - Bot Smarter Medic State ============//
//
// Purpose: A new bot state for Medics to heal teammates.
//
//=============================================================================//

#ifndef FF_BOT_HEAL_TEAMMATE_H
#define FF_BOT_HEAL_TEAMMATE_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When a Medic bot is healing a teammate.
 */
class HealTeammateState : public BotState
{
public:
	HealTeammateState(void);
	virtual ~HealTeammateState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

	// Method for CFFBot to explicitly set a heal target before transitioning to this state
	void RequestHealTarget( CFFPlayer *target );

private:
	bool ValidateHealTarget( CFFBot *me ); // Validates if the current target is still good
	bool FindAndSetHealTarget( CFFBot *me ); // Finds a new target and sets m_healTarget

	CHandle<CFFPlayer> m_healTarget;
	CountdownTimer m_findPlayerTimer;   // Timer to delay searching for new players if current target is lost or none initially
	CountdownTimer m_repathTimer;       // Timer to periodically check path if stuck moving to target
};

#endif // FF_BOT_HEAL_TEAMMATE_H
