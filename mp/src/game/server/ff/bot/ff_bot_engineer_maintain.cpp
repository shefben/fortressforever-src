//========= Fortress Forever Bot =============================================//
//
// CFFBotEngineerMaintain — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_engineer_maintain.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_buildableobject.h"
#include "ff_buildable_sentrygun.h"
#include "ff_buildable_dispenser.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "ammodef.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


// Range under which the engineer is "in position" to spanner-swing buildings.
#define FFBOT_MAINTAIN_TOO_FAR	75.0f

// Cells (FF AMMO_CELLS) needed for a spanner swing to do useful upgrade work.
#define FFBOT_MAINTAIN_MIN_CELLS	10


//-----------------------------------------------------------------------------
CFFBotEngineerMaintain::CFFBotEngineerMaintain( void )
{
	m_isCheckingForSpies = false;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerMaintain::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotEngineerMaintain::Update( CFFBot *me, float interval )
{
	CFFSentryGun *sg = me->GetSentryGun();
	CFFDispenser *disp = me->GetDispenser();

	if ( !sg )
		return Done( "Sentry destroyed — parent should rebuild" );

	// Equip spanner — repair/upgrade requires melee.
	CBaseCombatWeapon *spanner = me->Weapon_OwnsThisType( "ff_weapon_spanner" );
	CFFWeaponBase *active = me->GetActiveFFWeapon();
	if ( spanner && ( !active || !FStrEq( active->GetClassname(), "ff_weapon_spanner" ) ) )
	{
		me->Weapon_Switch( spanner );
	}

	// Spy paranoia — every 8-12s, do a 180° glance behind. FF doesn't have
	// a built-in "look behind" gesture; we just briefly aim the body
	// opposite of our forward.
	if ( !m_isCheckingForSpies && m_spyCheckTimer.IsElapsed() )
	{
		m_isCheckingForSpies = true;
		m_spyCheckTimer.Start( RandomFloat( 0.5f, 1.0f ) );
		Vector myForward;
		me->EyeVectors( &myForward );
		const Vector behind = me->EyePosition() - myForward * 200.0f;
		IBody *body = me->GetBodyInterface();
		if ( body )
			body->AimHeadTowards( behind, IBody::IMPORTANT, 0.5f, NULL, "Spy paranoia check" );
		return Continue();
	}
	if ( m_isCheckingForSpies && m_spyCheckTimer.IsElapsed() )
	{
		m_isCheckingForSpies = false;
		m_spyCheckTimer.Start( RandomFloat( 8.0f, 12.0f ) );
	}

	// Pick our station — between SG and dispenser if both exist, otherwise
	// just at SG.
	Vector station;
	if ( disp )
	{
		station = ( sg->GetAbsOrigin() + disp->GetAbsOrigin() ) * 0.5f;
	}
	else
	{
		station = sg->GetAbsOrigin();
	}

	const float distToStation = ( me->GetAbsOrigin() - station ).Length();

	if ( distToStation > FFBOT_MAINTAIN_TOO_FAR )
	{
		// Move to station.
		if ( m_repathTimer.IsElapsed() )
		{
			m_repathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
			m_path.Compute( me, station, cost );
		}
		// FIX 1 — single movement authority. CanDrivePath publishes the
		// path goal for the aim driver and refuses while the movement
		// arbiter owns locomotion, so this can never issue a second,
		// contradictory Approach() in the same tick.
		if ( FFBotHelpers::CanDrivePath( me, m_path ) )
			m_path.Update( me );
		return Continue();
	}

	// At station — pick the highest-priority work target and swing spanner
	// at it.
	const int cellsType = GetAmmoDef()->Index( AMMO_CELLS );
	const int cells = ( cellsType >= 0 ) ? me->GetAmmoCount( cellsType ) : 0;
	if ( cells < FFBOT_MAINTAIN_MIN_CELLS )
	{
		return Done( "Out of cells — parent should resupply" );
	}

	CBaseEntity *workTarget = sg;

	// Sapped / damaged building takes priority.
	if ( sg->IsSabotaged() || sg->GetHealth() < sg->GetMaxHealth() )
		workTarget = sg;
	else if ( disp && ( disp->IsSabotaged() || disp->GetHealth() < disp->GetMaxHealth() ) )
		workTarget = disp;
	else if ( sg->GetLevel() < 3 )
		workTarget = sg;
	else if ( disp )
		workTarget = disp;	// keep dispenser stocked

	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		body->AimHeadTowards( workTarget->WorldSpaceCenter(),
			IBody::CRITICAL, 1.0f, NULL, "Working on buildings" );
	}
	me->PressFireButton();

	return Continue();
}
