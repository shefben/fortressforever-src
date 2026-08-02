//========= Fortress Forever Bot =============================================//
//
// CFFBotMedicFollow — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_medic_follow.h"
#include "ff_bot_ctf.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_player.h"
#include "ff_weapon_base.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Stay roughly here from the patient — close enough to medkit-heal at any
// moment, far enough not to bump them.
#define FFBOT_FOLLOW_TARGET_RANGE	120.0f
// Outside this range we sprint to close.
#define FFBOT_FOLLOW_MAX_RANGE		300.0f
// Within this we melee-heal.
#define FFBOT_HEAL_RANGE			80.0f


static bool IsFrontLineMedicPatientClass( int classSlot )
{
	return classSlot == CLASS_HWGUY ||
	       classSlot == CLASS_SOLDIER ||
	       classSlot == CLASS_PYRO ||
	       classSlot == CLASS_DEMOMAN;
}


//-----------------------------------------------------------------------------
// Pick the highest-priority alive friendly front-line patient. Class
// priority matches TFBot's heal preference, mapped to FF roles:
//   1. HWGuy (slow, soaks damage, chronically wants heals)
//   2. Soldier
//   3. Pyro
//   4. Demoman
// Skip support classes (sniper/engineer/spy/medic/civilian) — they can rely
// on the opportunistic CFFBotHealTeammate path.
//-----------------------------------------------------------------------------
CFFPlayer *CFFBotMedicFollow::PickPatient( CFFBot *me ) const
{
	const int myTeam = me->GetTeamNumber();
	const Vector myPos = me->GetAbsOrigin();

	CFFPlayer *flagCarrier = FFBotHelpers::FindTeammateCarryingEnemyFlag( me );
	if ( flagCarrier )
		return flagCarrier;

	static const int kPriority[] = {
		CLASS_HWGUY, CLASS_SOLDIER, CLASS_PYRO, CLASS_DEMOMAN, 0
	};

	for ( int p = 0; kPriority[ p ] != 0; ++p )
	{
		const int wantClass = kPriority[ p ];
		CFFPlayer *bestForClass = NULL;
		float bestDistSq = FLT_MAX;

		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CFFPlayer *pp = ToFFPlayer( UTIL_PlayerByIndex( i ) );
			if ( !pp || pp == me || !pp->IsAlive() )
				continue;
			if ( pp->GetTeamNumber() != myTeam )
				continue;
			if ( pp->GetClassSlot() != wantClass )
				continue;

			const float dSq = ( pp->GetAbsOrigin() - myPos ).LengthSqr();
			if ( dSq < bestDistSq )
			{
				bestDistSq = dSq;
				bestForClass = pp;
			}
		}
		if ( bestForClass )
			return bestForClass;
	}

	return NULL;
}


//-----------------------------------------------------------------------------
CFFBotMedicFollow::CFFBotMedicFollow( void )
{
	m_patient = NULL;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMedicFollow::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );

	// Equip medkit so movement-time we're ready to swing immediately.
	CBaseCombatWeapon *medkit = me->Weapon_OwnsThisType( "ff_weapon_medkit" );
	if ( medkit )
		me->Weapon_Switch( medkit );

	m_patient = PickPatient( me );
	if ( !m_patient.Get() )
	{
		// No front-line patient on the team yet. Fall back to CtfObjective
		// so the medic at least carries the flag / runs the objective
		// instead of Done()ing out of the root action and freezing in
		// spawn. (Done from the root action terminates the Behavior's
		// child; the bot stops driving any path.)
		return ChangeTo( new CFFBotCtfObjective,
			"No patient yet — defaulting to CTF objective" );
	}

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMedicFollow::Update( CFFBot *me, float interval )
{
	if ( FFBotHelpers::IsBotCarryingFlag( me, NULL ) )
	{
		return ChangeTo( new CFFBotCtfObjective,
			"Medic carrying flag — running objective" );
	}

	CFFPlayer *patient = m_patient.Get();

	CFFPlayer *flagCarrier = FFBotHelpers::FindTeammateCarryingEnemyFlag( me );
	if ( flagCarrier && flagCarrier != patient )
	{
		m_patient = flagCarrier;
		m_repathTimer.Invalidate();
		patient = flagCarrier;
	}
	else if ( !flagCarrier && patient &&
	          !IsFrontLineMedicPatientClass( patient->GetClassSlot() ) )
	{
		m_patient = PickPatient( me );
		m_repathTimer.Invalidate();
		patient = m_patient.Get();
		if ( !patient )
		{
			return ChangeTo( new CFFBotCtfObjective,
				"Flag carrier no longer needs escort — running objective" );
		}
	}

	if ( !patient || !patient->IsAlive() )
	{
		// Patient died or vanished — try to pick another.
		if ( !m_pickPatientTimer.HasStarted() || m_pickPatientTimer.IsElapsed() )
		{
			m_pickPatientTimer.Start( 1.0f );
			m_patient = PickPatient( me );
			if ( !m_patient.Get() )
			{
				// No live front-liners — fall back to CtfObjective so
				// we continue contributing instead of standing still.
				// MedicFollow can be re-entered later by the main
				// action when a patient class respawns (TODO when we
				// add medic-respawn-trigger support).
				return ChangeTo( new CFFBotCtfObjective,
					"Patient died and no replacement — running objective" );
			}
			patient = m_patient.Get();
		}
		else
		{
			return Continue();
		}
	}

	const Vector patientPos = patient->GetAbsOrigin();
	const Vector myPos = me->GetAbsOrigin();
	const float distSq = ( patientPos - myPos ).LengthSqr();

	// In medkit melee range — heal swing.
	if ( distSq <= FFBOT_HEAL_RANGE * FFBOT_HEAL_RANGE )
	{
		CBaseCombatWeapon *medkit = me->Weapon_OwnsThisType( "ff_weapon_medkit" );
		CFFWeaponBase *active = me->GetActiveFFWeapon();
		if ( medkit && ( !active || !FStrEq( active->GetClassname(), "ff_weapon_medkit" ) ) )
		{
			me->Weapon_Switch( medkit );
		}

		IBody *body = me->GetBodyInterface();
		if ( body )
		{
			body->AimHeadTowards( patient, IBody::CRITICAL, 0.5f, NULL,
				"Healing patient" );
		}

		// Only swing if patient actually needs HP — no point spamming a full one.
		if ( patient->GetHealth() < patient->GetMaxHealth() )
		{
			me->PressFireButton( 0.2f );
		}

		// Don't path while healing — locomotor would drift.
		m_path.Invalidate();
		return Continue();
	}

	// Out of melee — close the gap.
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 0.3f, 0.6f ) );
		// FIX 7 — repath hysteresis: leave a working path alone while the bot
		// is actually travelling it. Volatile cost terms (combat intensity,
		// grenade danger, ammo/health discounts, recent-stuck penalties) used
		// to flip A* between near-equal lanes on consecutive repaths.
		if ( FFBotHelpers::ShouldRecomputePath( me, m_path, patientPos ) )
		{
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			m_path.Compute( me, patientPos, cost );
		}
	}
	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me );

	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMedicFollow::OnResume( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	m_repathTimer.Invalidate();
	return Continue();
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotMedicFollow::ShouldHurry( const INextBot *me ) const
{
	return ANSWER_YES;	// don't lose the patient
}


//-----------------------------------------------------------------------------
QueryResultType CFFBotMedicFollow::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;	// stick with patient
}
