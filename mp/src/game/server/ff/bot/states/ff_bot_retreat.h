//========= Fortress Forever - Bot Retreat State ============//
//
// Purpose: Implements the bot state for retreating when health is low.
//
//=============================================================================//

#ifndef FF_BOT_RETREAT_H
#define FF_BOT_RETREAT_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For CFFBot and BotState

//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot's health is low and it needs to retreat.
 */
class RetreatState : public BotState
{
public:
	RetreatState(void);
	virtual ~RetreatState() { }

	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const;

private:
	Vector m_retreatSpot;							// The position we are retreating to
	CountdownTimer m_repathTimer;					// Timer to prevent rapid repathing if stuck
	CountdownTimer m_waitAtRetreatSpotTimer;		// How long to wait at the spot once reached
	bool m_isAtRetreatSpot;						// True if we have reached our retreat spot
};

#endif // FF_BOT_RETREAT_H
