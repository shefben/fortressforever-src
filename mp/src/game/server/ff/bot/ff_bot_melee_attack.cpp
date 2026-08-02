//========= Fortress Forever Bot =============================================//
//
// CFFBotMeleeAttack — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_melee_attack.h"
#include "ff_bot_helpers.h"
#include "ff_bot_path_cost.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "NextBotKnownEntity.h"
#include "NextBotInterface.h"
#include "NextBotVisionInterface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


ConVar ff_bot_melee_attack_abandon_range( "ff_bot_melee_attack_abandon_range",
	"500", FCVAR_CHEAT,
	"If threat is farther away than this, melee attack ends and parent action resumes." );


//-----------------------------------------------------------------------------
// Per-class melee weapon classnames. Indexed by class slot.
//-----------------------------------------------------------------------------
static const char *MeleeWeaponForClass( int classSlot )
{
	switch ( classSlot )
	{
	case CLASS_SPY:      return "ff_weapon_knife";
	case CLASS_ENGINEER: return "ff_weapon_spanner";
	case CLASS_MEDIC:    return "ff_weapon_medkit";
	case CLASS_CIVILIAN: return "ff_weapon_umbrella";
	default:             return "ff_weapon_crowbar";
	}
}


//-----------------------------------------------------------------------------
CFFBotMeleeAttack::CFFBotMeleeAttack( float giveUpRange )
{
	m_giveUpRange = ( giveUpRange < 0.0f )
		? ff_bot_melee_attack_abandon_range.GetFloat()
		: giveUpRange;
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMeleeAttack::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_path.SetMinLookAheadDistance( me->GetDesiredPathLookAheadRange() );
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMeleeAttack::Update( CFFBot *me, float interval )
{
	IVision *vision = me->GetVisionInterface();
	const CKnownEntity *threat = vision ? vision->GetPrimaryKnownThreat() : NULL;

	if ( threat == NULL || !threat->GetEntity() )
		return Done( "No threat" );

	const Vector lkp = threat->GetLastKnownPosition();
	if ( !me->IsRangeLessThan( lkp, m_giveUpRange ) )
		return Done( "Threat too far away for melee" );

	// Switch to our melee weapon if we own one.
	const char *meleeName = MeleeWeaponForClass( me->GetClassSlot() );
	if ( meleeName )
	{
		CBaseCombatWeapon *melee = me->Weapon_OwnsThisType( meleeName );
		CFFWeaponBase *active = me->GetActiveFFWeapon();
		if ( melee && ( !active || !FStrEq( active->GetClassname(), meleeName ) ) )
		{
			me->Weapon_Switch( melee );
		}
	}

	// Aim is handled per-tick at MainAction. Just keep swinging.
	me->PressFireButton();

	// Chase the target.
	CFFBotPathCost cost( me, FFBOT_FASTEST_ROUTE );
	// FIX 1 — single movement authority. CanDrivePath publishes the
	// path goal for the aim driver and refuses while the movement
	// arbiter owns locomotion, so this can never issue a second,
	// contradictory Approach() in the same tick.
	if ( FFBotHelpers::CanDrivePath( me, m_path ) )
		m_path.Update( me, threat->GetEntity(), cost );

	return Continue();
}
