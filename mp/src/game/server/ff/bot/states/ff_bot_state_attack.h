#ifndef FF_BOT_STATE_ATTACK_H
#define FF_BOT_STATE_ATTACK_H

#include "../ff_bot.h" // For BotState and CFFBot declaration
#include "utlcommon.h" // For CountdownTimer

//--------------------------------------------------------------------------------------------------------------
/**
 * When a bot has an enemy and is attempting to kill it
 */
class AttackState : public BotState
{
public:
	virtual void OnEnter( CFFBot *bot );
	virtual void OnUpdate( CFFBot *bot );
	virtual void OnExit( CFFBot *bot );
	virtual const char *GetName( void ) const { return "Attack"; }

	void SetCrouchAndHold( bool crouch ) { m_crouchAndHold = crouch; }

protected:
	enum DodgeStateType
	{
		STEADY_ON,
		SLIDE_LEFT,
		SLIDE_RIGHT,
		JUMP,
		NUM_ATTACK_STATES
	};
	DodgeStateType m_dodgeState;
	float m_nextDodgeStateTimestamp;

	CountdownTimer m_repathTimer;
	float m_scopeTimestamp;

	bool m_haveSeenEnemy;										///< false if we haven't yet seen the enemy since we started this attack (told by a friend, etc)
	bool m_isEnemyHidden;										///< true we if we have lost line-of-sight to our enemy
	float m_reacquireTimestamp;									///< time when we can fire again, after losing enemy behind cover
	float m_shieldToggleTimestamp;								///< time to toggle shield deploy state
	bool m_shieldForceOpen;										///< if true, open up and shoot even if in danger
	float m_pinnedDownTimestamp;								///< time when we'll consider ourselves "pinned down" by the enemy
	bool m_crouchAndHold;
	bool m_didAmbushCheck;
	bool m_shouldDodge;
	bool m_firstDodge;
	bool m_isCoward;											///< if true, we'll retreat if outnumbered during this fight
	CountdownTimer m_retreatTimer;

	void StopAttacking( CFFBot *bot );
	void Dodge( CFFBot *bot );
};

#endif // FF_BOT_STATE_ATTACK_H
