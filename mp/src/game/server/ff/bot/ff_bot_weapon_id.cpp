//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Weapon ID,
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ff_bot_weapon_id.h" // Include the new header
#include "../../shared/ff/weapons/ff_weapon_base.h" // For FFWeaponID (already included in .h but good for clarity)
#include "tier0/cstring.h" // For Q_stricmp

// WeaponClassTypeFF enum and BotWeaponTranslationFF struct are now defined in ff_bot_weapon_id.h

// TODO: Verify all entity class names and buy aliases against actual FF weapon definitions.
BotWeaponTranslationFF g_FFBotWeaponTranslations[] =
{
    { FF_WEAPON_SHOTGUN, "ff_weapon_shotgun", "shotgun", WEAPONCLASS_FF_SHOTGUN },
    { FF_WEAPON_SUPERSHOTGUN, "ff_weapon_supershotgun", "supershotgun", WEAPONCLASS_FF_SHOTGUN },
    { FF_WEAPON_NAILGUN, "ff_weapon_nailgun", "nailgun", WEAPONCLASS_FF_NAILGUN },
    { FF_WEAPON_SUPERNAILGUN, "ff_weapon_supernailgun", "supernailgun", WEAPONCLASS_FF_NAILGUN },
    { FF_WEAPON_GRENADELAUNCHER, "ff_weapon_grenadelauncher", "grenadelauncher", WEAPONCLASS_FF_GRENADELAUNCHER },
    { FF_WEAPON_PIPELAUNCHER, "ff_weapon_pipelauncher", "pipelauncher", WEAPONCLASS_FF_GRENADELAUNCHER },
    { FF_WEAPON_RPG, "ff_weapon_rpg", "rpg", WEAPONCLASS_FF_ROCKETLAUNCHER },
    { FF_WEAPON_FLAMETHROWER, "ff_weapon_flamethrower", "flamethrower", WEAPONCLASS_FF_FLAMETHROWER },
    { FF_WEAPON_IC, "ff_weapon_ic", "ic", WEAPONCLASS_FF_FLAMETHROWER }, // Incendiary Cannon
    { FF_WEAPON_ASSAULTCANNON, "ff_weapon_assaultcannon", "assaultcannon", WEAPONCLASS_FF_ASSAULTCANNON },
    { FF_WEAPON_SNIPERRIFLE, "ff_weapon_sniperrifle", "sniperrifle", WEAPONCLASS_FF_SNIPERRIFLE },
    { FF_WEAPON_AUTORIFLE, "ff_weapon_autorifle", "autorifle", WEAPONCLASS_FF_SNIPERRIFLE },
    { FF_WEAPON_RAILGUN, "ff_weapon_railgun", "railgun", WEAPONCLASS_FF_RAILGUN },
    { FF_WEAPON_TRANQUILISER, "ff_weapon_tranquiliser", "tranq", WEAPONCLASS_FF_TRANQGUN },

    { FF_WEAPON_KNIFE, "ff_weapon_knife", "knife", WEAPONCLASS_FF_MELEE },
    { FF_WEAPON_CROWBAR, "ff_weapon_crowbar", "crowbar", WEAPONCLASS_FF_MELEE },
    { FF_WEAPON_AXE, "ff_weapon_axe", "axe", WEAPONCLASS_FF_MELEE },
    { FF_WEAPON_SPANNER, "ff_weapon_spanner", "spanner", WEAPONCLASS_FF_MELEE },
    { FF_WEAPON_UMBRELLA, "ff_weapon_umbrella", "umbrella", WEAPONCLASS_FF_MELEE },

    { FF_WEAPON_MEDKIT, "ff_weapon_medkit", "medkit", WEAPONCLASS_FF_SUPPORT },
    { FF_WEAPON_JUMPGUN, "ff_weapon_jumpgun", "jumpgun", WEAPONCLASS_FF_JUMPGUN },
    { FF_WEAPON_TOMMYGUN, "ff_weapon_tommygun", "tommygun", WEAPONCLASS_FF_TOMMYGUN },

    { FF_WEAPON_NONE, NULL, NULL, WEAPONCLASS_FF_UNKNOWN } // Terminator
};

const char *WeaponIDToAliasFF(FFWeaponID id)
{
    for (int i=0; g_FFBotWeaponTranslations[i].id != FF_WEAPON_NONE; ++i)
    {
        if (g_FFBotWeaponTranslations[i].id == id)
            return g_FFBotWeaponTranslations[i].buyAlias;
    }
    return NULL;
}

FFWeaponID AliasToWeaponIDFF(const char *alias)
{
    if (!alias) return FF_WEAPON_NONE;
    for (int i=0; g_FFBotWeaponTranslations[i].id != FF_WEAPON_NONE; ++i)
    {
        if (g_FFBotWeaponTranslations[i].buyAlias && Q_stricmp(g_FFBotWeaponTranslations[i].buyAlias, alias) == 0)
            return g_FFBotWeaponTranslations[i].id;
        if (g_FFBotWeaponTranslations[i].entityClassName && Q_stricmp(g_FFBotWeaponTranslations[i].entityClassName, alias) == 0)
            return g_FFBotWeaponTranslations[i].id;
    }
    return FF_WEAPON_NONE;
}

WeaponClassTypeFF GetWeaponClassTypeFF(FFWeaponID id)
{
    for (int i=0; g_FFBotWeaponTranslations[i].id != FF_WEAPON_NONE; ++i)
    {
        if (g_FFBotWeaponTranslations[i].id == id)
            return g_FFBotWeaponTranslations[i].classType;
    }
    return WEAPONCLASS_FF_UNKNOWN;
}

const char *GetWeaponEntityClassName(FFWeaponID id)
{
    for (int i=0; g_FFBotWeaponTranslations[i].id != FF_WEAPON_NONE; ++i)
    {
        if (g_FFBotWeaponTranslations[i].id == id)
            return g_FFBotWeaponTranslations[i].entityClassName;
    }
    return NULL;
}

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
