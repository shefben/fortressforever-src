#ifndef FF_BOT_WEAPON_ID_H
#define FF_BOT_WEAPON_ID_H

#pragma once

// Forward declare FFWeaponID if its actual definition is in a deeper header not suitable for this file
// However, it's better to include the source of FFWeaponID directly.
#include "../../shared/ff/weapons/ff_weapon_base.h" // This should define FFWeaponID

enum WeaponClassTypeFF
{
    WEAPONCLASS_FF_UNKNOWN = 0,
    WEAPONCLASS_FF_MELEE,
    WEAPONCLASS_FF_SHOTGUN,
    WEAPONCLASS_FF_NAILGUN,
    WEAPONCLASS_FF_GRENADELAUNCHER, // For GL and Pipe
    WEAPONCLASS_FF_ROCKETLAUNCHER,  // For RPG
    WEAPONCLASS_FF_SNIPERRIFLE,
    WEAPONCLASS_FF_FLAMETHROWER,    // For Flamethrower and IC
    WEAPONCLASS_FF_ASSAULTCANNON,
    WEAPONCLASS_FF_RAILGUN,
    WEAPONCLASS_FF_TRANQGUN,
    WEAPONCLASS_FF_TOMMYGUN,        // Civilian Special
    WEAPONCLASS_FF_JUMPGUN,         // Scout Special
    WEAPONCLASS_FF_SUPPORT,         // For items like Medkit
    WEAPONCLASS_FF_SPECIAL,         // For unique items like Umbrella if not melee
    // Add more categories as needed
};

// Utility function forward declarations
const char *WeaponIDToAliasFF(FFWeaponID id);
FFWeaponID AliasToWeaponIDFF(const char *alias);
WeaponClassTypeFF GetWeaponClassTypeFF(FFWeaponID id);
const char *GetWeaponEntityClassName(FFWeaponID id);

#endif // FF_BOT_WEAPON_ID_H
