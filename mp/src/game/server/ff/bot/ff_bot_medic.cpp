//========= Fortress Forever Bot =============================================//
//
// CFFBotHealTeammate — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_medic.h"
#include "ff_bot_path_cost.h"
#include "ff_player.h"
#include "ff_weapon_base.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Range at which we consider ourselves "next to" the patient — medkit is a
// melee weapon, so we need to be in swing distance.
#define FFBOT_HEAL_RANGE			80.0f
// Hard cap on pursuit time. If we can't reach the patient in this long, give
// up so we resume offense.
#define FFBOT_HEAL_GIVE_UP_TIME		8.0f
// Stop healing once the patient is within this many HP of full — final 10hp
// of medkit chip is not worth the time investment.
#define FFBOT_HEAL_LEAVE_DEFICIT	5


//-----------------------------------------------------------------------------
CFFBotHealTeammate::CFFBotHealTeammate( CFFPlayer *target )
{
	m_target.Set( target );
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotHealTeammate::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( 100.0f );
	m_repathTimer.Invalidate();
	m_giveUpTimer.Start( FFBOT_HEAL_GIVE_UP_TIME );

	// Switch to medkit so primary attack heals on contact. Weapon_OwnsThisType
	// returns the existing medkit weapon (every medic spawns with one).
	CBaseCombatWeapon *medkit = me->Weapon_OwnsThisType( "ff_weapon_medkit" );
	if ( medkit )
		me->Weapon_Switch( medkit );

	return Continue();
}

//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotHealTeammate::Update( CFFBot *me, float interval )
{
	CBaseEntity *targetEnt = m_target.Get();
	CFFPlayer *target = targetEnt ? ToFFPlayer( targetEnt ) : NULL;
	if ( !target || !target->IsAlive() )
		return Done( "Heal target gone" );

	if ( target->GetTeamNumber() != me->GetTeamNumber() )
		return Done( "Heal target changed team" );

	if ( target->GetMaxHealth() - target->GetHealth() < FFBOT_HEAL_LEAVE_DEFICIT )
		return Done( "Heal target topped up" );

	if ( m_giveUpTimer.IsElapsed() )
		return Done( "Pursuit timed out" );

	const Vector myPos = me->GetAbsOrigin();
	const Vector targetPos = target->GetAbsOrigin();
	const float distSq = ( targetPos - myPos ).LengthSqr();

	// In melee range — face them and swing the medkit.
	if ( distSq < FFBOT_HEAL_RANGE * FFBOT_HEAL_RANGE )
	{
		IBody *body = me->GetBodyInterface();
		if ( body )
		{
			Vector aim = target->WorldSpaceCenter();
			body->AimHeadTowards( aim, IBody::CRITICAL, 0.5f, NULL, "Healing teammate" );
		}

		// Make sure we still have the medkit out (combat aim/fire can switch
		// weapon implicitly via input frames).
		CFFWeaponBase *active = me->GetActiveFFWeapon();
		if ( !active || active->GetWeaponID() != FF_WEAPON_MEDKIT )
		{
			CBaseCombatWeapon *medkit = me->Weapon_OwnsThisType( "ff_weapon_medkit" );
			if ( medkit )
				me->Weapon_Switch( medkit );
		}

		// Swing — primary attack with medkit heals teammates / melee-strikes
		// enemies (latter shouldn't trigger here, target is teammate).
		me->PressFireButton( 0.2f );

		// Don't path while in melee range — locomotor would try to keep
		// distance.
		m_path.Invalidate();
		return Continue();
	}

	// Too far — path toward patient. Recompute occasionally so a moving
	// patient (running for cover) is tracked.
	if ( m_repathTimer.IsElapsed() )
	{
		m_repathTimer.Start( RandomFloat( 0.5f, 1.0f ) );
		CFFBotPathCost cost( me, FFBOT_DEFAULT_ROUTE );
		m_path.Compute( me, targetPos, cost );
	}
	m_path.Update( me );
	return Continue();
}
