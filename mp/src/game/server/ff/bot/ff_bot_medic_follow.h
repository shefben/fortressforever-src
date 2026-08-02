//========= Fortress Forever Bot =============================================//
//
// CFFBotMedicFollow — long-term medic squad behavior. Pick a friendly
// front-line target (Soldier/HWGuy/Demoman/Pyro), shadow them, and
// apply medkit heals when they're hurt.
//
// Differs from CFFBotHealTeammate (which is opportunistic single-target
// heal): MedicFollow is the medic's *primary* default behavior, replacing
// "wander aimlessly with a medkit." It only releases the patient when they
// die, are out of reach, or the medic itself enters a survival-critical
// state.
//
// Inspired by hl2_src tf_bot_medic_heal but radically simpler since FF
// medic heals via melee medkit (no medigun beam, no uber, no continuous
// healing target tracking).
//
//===========================================================================//

#ifndef FF_BOT_MEDIC_FOLLOW_H
#define FF_BOT_MEDIC_FOLLOW_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBotBehavior.h"
#include "Path/NextBotPathFollow.h"
#include "ff_bot.h"

class CFFPlayer;

class CFFBotMedicFollow : public Action< CFFBot >
{
public:
	CFFBotMedicFollow( void );

	virtual ActionResult< CFFBot > OnStart( CFFBot *me, Action< CFFBot > *priorAction ) OVERRIDE;
	virtual ActionResult< CFFBot > Update( CFFBot *me, float interval ) OVERRIDE;
	virtual ActionResult< CFFBot > OnResume( CFFBot *me, Action< CFFBot > *interruptingAction ) OVERRIDE;

	virtual QueryResultType ShouldHurry( const INextBot *me ) const OVERRIDE;
	virtual QueryResultType ShouldRetreat( const INextBot *me ) const OVERRIDE;

	virtual const char *GetName( void ) const OVERRIDE { return "MedicFollow"; }

private:
	CHandle< CFFPlayer > m_patient;
	PathFollower m_path;
	CountdownTimer m_repathTimer;
	CountdownTimer m_pickPatientTimer;

	CFFPlayer *PickPatient( CFFBot *me ) const;
};

#endif // FF_BOT_MEDIC_FOLLOW_H
