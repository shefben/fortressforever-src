//========= Fortress Forever - Bot Demoman Lay Sticky Trap State ============//
//
// Purpose: Implements the bot state for Demomen laying stickybomb traps.
//
//=============================================================================//

#ifndef FF_BOT_LAY_STICKY_TRAP_H
#define FF_BOT_LAY_STICKY_TRAP_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When a Demoman bot is laying a stickybomb trap.
 */
class LayStickyTrapState : public BotState
{
public:
	LayStickyTrapState(void);
	virtual ~LayStickyTrapState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

	void SetTrapLocation( const Vector &pos ) { m_targetTrapLocation = pos; }

private:
	Vector m_targetTrapLocation;        // The actual point on the ground to aim stickies at.
	Vector m_laySpot;                   // The spot where the Demoman stands to lay the trap.
	bool m_isAtLaySpot;               // True if the bot has reached m_laySpot.
	int m_laidStickiesInTrap;         // How many stickies have been laid for the current trap.
	CountdownTimer m_layIntervalTimer;    // Timer between laying individual stickies.
	CountdownTimer m_repathTimer;           // Timer to periodically repath if stuck moving to lay spot.

	static const int STICKIES_PER_TRAP = 3; // How many stickies to lay for a standard trap.
	static const float LAY_INTERVAL = 0.75f; // Delay between laying each sticky (allows for weapon refire, arm time).
	static const float POSITIONING_DISTANCE = 200.0f; // How far from trap target to stand.
};

#endif // FF_BOT_LAY_STICKY_TRAP_H
