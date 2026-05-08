//========= Fortress Forever Bot =============================================//
//
// FFBotWeapon — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_weapon.h"
#include "ff_bot.h"
#include "ff_player.h"
#include "ff_weapon_base.h"
#include "shareddefs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Per-class weapon preferences. Indexed by CLASS_SCOUT..CLASS_CIVILIAN (1..10).
// `closeRange` is the weapon to use when the threat is < CLOSE_RANGE_LIMIT
// units away; `longRange` is the default and what we equip on spawn.
//-----------------------------------------------------------------------------
struct ClassWeaponPrefs
{
	const char *closeRange;
	const char *longRange;
};

#define FFBOT_CLOSE_RANGE_LIMIT		500.0f

static const ClassWeaponPrefs kClassPrefs[ 11 ] = {
	/* 0  unassigned */ { NULL, NULL },
	/* 1  Scout    */   { "ff_weapon_supershotgun", "ff_weapon_nailgun" },
	/* 2  Sniper   */   { "ff_weapon_autorifle",    "ff_weapon_sniperrifle" },
	/* 3  Soldier  */   { "ff_weapon_supershotgun", "ff_weapon_rpg" },
	/* 4  Demoman  */   { "ff_weapon_supershotgun", "ff_weapon_grenadelauncher" },
	/* 5  Medic    */   { "ff_weapon_supershotgun", "ff_weapon_supernailgun" },
	/* 6  HWGuy    */   { "ff_weapon_assaultcannon","ff_weapon_assaultcannon" },
	/* 7  Pyro     */   { "ff_weapon_flamethrower", "ff_weapon_ic" },
	/* 8  Spy      */   { "ff_weapon_tranquiliser", "ff_weapon_tranquiliser" },
	/* 9  Engineer */   { "ff_weapon_supershotgun", "ff_weapon_railgun" },
	/* 10 Civilian */   { "ff_weapon_umbrella",     "ff_weapon_umbrella" },
};


//-----------------------------------------------------------------------------
const char *FFBotWeapon::PickPreferredWeaponClass( int classSlot, float threatRange )
{
	if ( classSlot < CLASS_SCOUT || classSlot > CLASS_CIVILIAN )
		return NULL;

	const ClassWeaponPrefs &p = kClassPrefs[ classSlot ];

	// No threat → equip long-range / main.
	if ( threatRange < 0.0f )
		return p.longRange;

	// Engagement-range pick.
	return ( threatRange < FFBOT_CLOSE_RANGE_LIMIT ) ? p.closeRange : p.longRange;
}


//-----------------------------------------------------------------------------
// Helper: does the weapon have ammo to fire? Falls back to true when we
// can't determine (better to attempt fire than pass on a working weapon).
//-----------------------------------------------------------------------------
static bool WeaponHasAmmo( CFFBot *me, CBaseCombatWeapon *w )
{
	if ( !w )
		return false;

	const int primaryAmmoType = w->GetPrimaryAmmoType();
	if ( primaryAmmoType < 0 )
		return true;	// melee or no-ammo weapon

	if ( w->Clip1() > 0 )
		return true;
	if ( me->GetAmmoCount( primaryAmmoType ) > 0 )
		return true;

	return false;
}


//-----------------------------------------------------------------------------
// Try to issue a weapon switch toward the preferred classname. Throttled to
// once every 0.4s to avoid switch-thrashing on rapid range changes.
//-----------------------------------------------------------------------------
bool FFBotWeapon::TrySwitchToPreferred( CFFBot *me, float threatRange )
{
	if ( !me || !me->IsAlive() )
		return false;

	if ( me->m_weaponSwitchTimer.HasStarted() && !me->m_weaponSwitchTimer.IsElapsed() )
		return false;

	const int classSlot = me->GetClassSlot();
	const char *preferred = PickPreferredWeaponClass( classSlot, threatRange );
	if ( !preferred )
		return false;

	CFFWeaponBase *active = me->GetActiveFFWeapon();
	if ( active && FStrEq( active->GetClassname(), preferred ) && WeaponHasAmmo( me, active ) )
	{
		// Already on the right weapon and it has ammo — re-arm timer so we
		// don't thrash.
		me->m_weaponSwitchTimer.Start( 0.4f );
		return false;
	}

	CBaseCombatWeapon *target = me->Weapon_OwnsThisType( preferred );
	if ( !target )
	{
		// We don't carry this weapon at all (class loadout mismatch).
		// Don't bother re-checking for 2s.
		me->m_weaponSwitchTimer.Start( 2.0f );
		return false;
	}

	if ( !WeaponHasAmmo( me, target ) )
	{
		// Out of ammo — try the OTHER preference (close vs long flip)
		// before giving up.
		const ClassWeaponPrefs &p = kClassPrefs[ classSlot ];
		const char *fallback = ( preferred == p.closeRange ) ? p.longRange : p.closeRange;
		CBaseCombatWeapon *backup = ( fallback && fallback != preferred )
			? me->Weapon_OwnsThisType( fallback ) : NULL;
		if ( backup && WeaponHasAmmo( me, backup ) && backup != active )
		{
			me->Weapon_Switch( backup );
			me->m_weaponSwitchTimer.Start( 0.4f );
			return true;
		}
		me->m_weaponSwitchTimer.Start( 1.0f );
		return false;
	}

	me->Weapon_Switch( target );
	me->m_weaponSwitchTimer.Start( 0.4f );
	return true;
}


//-----------------------------------------------------------------------------
void FFBotWeapon::EquipMainWeaponForSpawn( CFFBot *me )
{
	if ( !me )
		return;
	const char *main = PickPreferredWeaponClass( me->GetClassSlot(), -1.0f );
	if ( !main )
		return;
	CBaseCombatWeapon *target = me->Weapon_OwnsThisType( main );
	if ( target && target != me->GetActiveFFWeapon() )
		me->Weapon_Switch( target );
}
