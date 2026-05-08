//========= Fortress Forever Bot =============================================//
//
// FFBotWeapon — class-loadout-aware weapon selection. Picks the right tool
// for the current engagement: sniper rifle at long range, supershotgun in
// close, etc. Without this, the bot uses whatever weapon Spawn() leaves
// equipped (often the wrong one).
//
//===========================================================================//

#ifndef FF_BOT_WEAPON_H
#define FF_BOT_WEAPON_H
#ifdef _WIN32
#pragma once
#endif

class CFFBot;

namespace FFBotWeapon
{
	// Pick the weapon classname (e.g. "ff_weapon_rpg") this class wants to
	// use given the engagement range. Returns NULL when there's no class-
	// specific preference (caller should leave the active weapon alone).
	// `threatRange < 0` means "no current threat" — returns the class's
	// idle/main weapon.
	const char *PickPreferredWeaponClass( int classSlot, float threatRange );

	// Try to switch the bot to its preferred weapon. Throttled by the bot's
	// m_weaponSwitchTimer so we don't churn weapon switches every tick.
	// Returns true if a switch was issued.
	bool TrySwitchToPreferred( CFFBot *me, float threatRange );

	// Equip the class's main weapon (treated as long-range preference) once
	// at spawn. Called from FFBotClass::Update first-tick init.
	void EquipMainWeaponForSpawn( CFFBot *me );
}

#endif // FF_BOT_WEAPON_H
