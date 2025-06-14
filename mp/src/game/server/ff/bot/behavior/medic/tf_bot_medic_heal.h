//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_medic_heal.h
// Heal a teammate
// Michael Booth, February 2009

#ifndef FF_BOT_MEDIC_HEAL_H
#define FF_BOT_MEDIC_HEAL_H

#include "Path/NextBotChasePath.h"

class CWeaponMedigun;

class CFFBotMedicHeal : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual ActionResult< CFFBot >	OnResume( CFFBot *me, Action< CFFBot > *interruptingAction );

	virtual EventDesiredResult< CFFBot > OnStuck( CFFBot *me );
	virtual EventDesiredResult< CFFBot > OnMoveToSuccess( CFFBot *me, const Path *path );
	virtual EventDesiredResult< CFFBot > OnMoveToFailure( CFFBot *me, const Path *path, MoveToFailureType reason );
	virtual EventDesiredResult< CFFBot > OnActorEmoted( CFFBot *me, CBaseCombatCharacter *emoter, int emote );

	virtual QueryResultType ShouldHurry( const INextBot *me ) const;					// are we in a hurry?
	virtual QueryResultType ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;	// should we attack "them"?
	virtual QueryResultType ShouldRetreat( const INextBot *bot ) const;

	virtual const char *GetName( void ) const	{ return "Heal"; };

private:
	ChasePath m_chasePath;

	CFFPlayer *SelectPatient( CFFBot *me, CFFPlayer *current );
	CountdownTimer m_changePatientTimer;

	CountdownTimer m_delayUberTimer;

	CHandle< CFFPlayer > m_patient;
	Vector m_patientAnchorPos;							// a spot where the patient was, to track if they are moving
	CountdownTimer m_isPatientRunningTimer;
	bool IsPatientRunning( void ) const;

	bool IsStable( CFFPlayer *patient ) const;			// return true if the given patient is healthy and safe for now

	CTFNavArea *FindCoverArea( CFFBot *me );
	CTFNavArea *m_coverArea;
	CountdownTimer m_coverTimer;
	PathFollower m_coverPath;

	void ComputeFollowPosition( CFFBot *me );
	Vector m_followGoal;

	bool IsVisibleToEnemy( CFFBot *me, const Vector &where ) const;

	bool IsReadyToDeployUber( const CWeaponMedigun* pMedigun ) const;

	bool IsGoodUberTarget( CFFPlayer *who ) const;

	bool CanDeployUber( CFFBot *me, const CWeaponMedigun* pMedigun ) const;
};

inline bool CFFBotMedicHeal::IsPatientRunning( void ) const
{
	return m_isPatientRunningTimer.IsElapsed() ? false : true;
}


#endif // FF_BOT_MEDIC_HEAL_H
