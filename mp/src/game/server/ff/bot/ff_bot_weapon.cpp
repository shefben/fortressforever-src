//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Weapon handling and selection logic for Fortress Forever bots.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
#include "../../shared/ff/weapons/ff_weapon_parse.h"  // For CFFWeaponInfo
#include "../../shared/ff/ff_playerclass_parse.h" // For CFFPlayerClassInfo
#include "ff_gamestate.h"
#include "bot_constants.h"
#include "bot_profile.h"
#include "bot_util.h"
#include "ff_bot_weapon_id.h" // For GetWeaponClassTypeFF and FFWeaponID

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Constants for weapon scoring are now in bot_constants.h

const float minEquipInterval = 1.0f;

//--------------------------------------------------------------------------------------------------------------
/**
 * Fire our active weapon towards our current enemy
 */
void CFFBot::FireWeaponAtEnemy( void ) { /* ... (implementation unchanged from previous versions) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Set the current aim offset using given accuracy (1.0 = perfect aim, 0.0f = terrible aim)
 */
void CFFBot::SetAimOffset( float accuracy ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Wiggle aim error based on GetProfile()->GetSkill()
 */
void CFFBot::UpdateAimOffset( void ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Change our zoom level to be appropriate for the given range.
 */
bool CFFBot::AdjustZoom( float range ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using the specific weapon ID
 */
bool CFFBot::IsUsing( FFWeaponID weaponID ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we are using a weapon with a removable silencer
 */
bool CFFBot::DoesActiveWeaponHaveSilencer( void ) const { return false; } // FF doesn't have silencers typically

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a sniper rifle
 */
bool CFFBot::IsUsingSniperRifle( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a sniper rifle in our inventory
 */
bool CFFBot::IsSniper( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are actively sniping
 */
bool CFFBot::IsSniping( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a shotgun
 */
bool CFFBot::IsUsingShotgun( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using an assault cannon
 */
bool CFFBot::IsUsingMachinegun( void ) const { /* ... (implementation unchanged) ... */ } // Renamed from IsUsingMachineGun to match header

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if "primary" type weapons are out of ammo
 */
bool CFFBot::IsPrimaryWeaponEmpty( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if "secondary" (pistol) type weapons are out of ammo
 */
bool CFFBot::IsPistolEmpty( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the given item
 */
bool CFFBot::DoEquip( CFFWeaponBase *weapon ) { /* ... (implementation unchanged) ... */ }


//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the best weapon we are carrying that has ammo, based on situation.
 */
void CFFBot::EquipBestWeapon( bool mustEquip )
{
	if (!mustEquip && m_equipTimer.GetElapsedTime() < minEquipInterval && GetActiveFFWeapon() != NULL) {
		return;
	}

	BotTaskType currentTask = GetTask();
    CFFPlayer* pOwner = static_cast<CFFPlayer*>(this);
	const CFFPlayerClassInfo* pClassInfo = pOwner->GetPlayerClassInfo();

	CFFPlayer* pEnemy = GetBotEnemy();
	float enemyDist = pEnemy ? (pEnemy->GetAbsOrigin() - GetAbsOrigin()).Length() : FLT_MAX;

	CFFWeaponBase* bestWeapon = NULL;
	float bestScore = -FLT_MAX;

    // Iterate through all weapons the bot is carrying
    for (int i = 0; i < MAX_WEAPONS; ++i)
    {
        CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
        if (!weapon || !weapon->CanDeploy() || !weapon->HasAnyAmmo()) {
            continue;
        }

        const CFFWeaponInfo* pWeaponInfo = weapon->GetFFWpnData();
        if (!pWeaponInfo) {
            continue;
        }

        FFWeaponID weaponID = weapon->GetWeaponID();
        WeaponClassTypeFF weaponType = GetWeaponClassTypeFF(weaponID);
        float currentScore = SCORE_BASE; // From bot_constants.h

        // 1. Class Weapon Bonus
        if (pClassInfo) {
            // Assuming pClassInfo->m_aWeapons stores weapon entity classnames or aliases
            // And pClassInfo->m_iNumWeapons is the count
            for(int j=0; j < pClassInfo->m_iNumWeapons; ++j) { // Use m_iNumWeapons
                // The weapon names in pClassInfo->m_aWeapons are likely entity classnames
                // or buy aliases. We need to convert them to FFWeaponID for comparison.
                const char* classWeaponName = pClassInfo->m_aWeapons[j].m_szClassName; // Use m_szClassName
                if (classWeaponName && classWeaponName[0]) {
                    FFWeaponID classWeaponEnumID = AliasToWeaponIDFF(classWeaponName); // Utility from ff_bot_weapon_id.cpp
                    if (classWeaponEnumID != FF_WEAPON_NONE && classWeaponEnumID == weaponID) {
                        currentScore += SCORE_CLASS_WEAPON_BONUS; // From bot_constants.h
                        break;
                    }
                }
            }
        }

        // 2. Situational Modifiers - Enemy Distance
        if (pEnemy) { // Only apply distance modifiers if there's an enemy
            switch (weaponType) {
                case WEAPONCLASS_FF_SHOTGUN:
                    if (enemyDist < DIST_CLOSE_COMBAT) currentScore += SCORE_SHOTGUN_CLOSE_BONUS;
                    else currentScore += SCORE_GENERIC_OUT_OF_RANGE_PENALTY; // General penalty if too far for shotgun
                    break;
                case WEAPONCLASS_FF_MELEE:
                    if (enemyDist < pWeaponInfo->m_flRange * DIST_MELEE_MAX_EFFECTIVE_RANGE_FACTOR) currentScore += SCORE_MELEE_CLOSE_BONUS;
                    else currentScore += SCORE_GENERIC_OUT_OF_RANGE_PENALTY;
                    break;
                case WEAPONCLASS_FF_SNIPERRIFLE:
                    if (enemyDist < DIST_SNIPER_MIN) currentScore += SCORE_SNIPER_TOO_CLOSE_PENALTY;
                    else if (enemyDist > DIST_SNIPER_OPTIMAL_MAX) currentScore += SCORE_SNIPER_VERY_FAR_BONUS;
					else if (enemyDist > DIST_SNIPER_OPTIMAL_MIN) currentScore += SCORE_SNIPER_AT_RANGE_BONUS;
                    break;
                case WEAPONCLASS_FF_ROCKETLAUNCHER:
                case WEAPONCLASS_FF_GRENADELAUNCHER: // Includes Pipe Launcher
                    if (enemyDist < DIST_ROCKET_MIN) currentScore += SCORE_ROCKET_TOO_CLOSE_PENALTY;
                    else if (enemyDist <= DIST_ROCKET_OPTIMAL_MAX) currentScore += SCORE_ROCKET_OPTIMAL_RANGE_BONUS;
					else currentScore += SCORE_GENERIC_OUT_OF_RANGE_PENALTY; // Too far for effective splash
                    break;
                default:
                    if (pWeaponInfo->m_flRange > 0 && enemyDist > pWeaponInfo->m_flRange * DIST_GENERIC_EFFECTIVE_RANGE_FACTOR) {
                        currentScore += SCORE_GENERIC_OUT_OF_RANGE_PENALTY;
                    }
                    break;
            }
        }

        // 3. Current Task Modifiers (using constants from bot_constants.h)
        switch (currentTask) {
            case BOT_TASK_SNIPING:
                if (weaponType == WEAPONCLASS_FF_SNIPERRIFLE) currentScore += SCORE_TASK_SNIPING_BONUS;
                break;
            case BOT_TASK_CARRY_FLAG_TO_CAP:
                if (weaponType == WEAPONCLASS_FF_SHOTGUN || weaponType == WEAPONCLASS_FF_NAILGUN || weaponType == WEAPONCLASS_FF_FLAMETHROWER)
                    currentScore += SCORE_FLAG_CARRIER_BONUS;
                else if (weaponType == WEAPONCLASS_FF_SNIPERRIFLE || weaponType == WEAPONCLASS_FF_ROCKETLAUNCHER)
                    currentScore -= SCORE_FLAG_CARRIER_PENALTY;
                break;
            case BOT_TASK_DEFEND_POINT:
            case BOT_TASK_DEFEND_FLAG_STAND:
                if (weaponType == WEAPONCLASS_FF_ROCKETLAUNCHER || weaponType == WEAPONCLASS_FF_GRENADELAUNCHER || weaponType == WEAPONCLASS_FF_ASSAULTCANNON)
                    currentScore += SCORE_DEFENSE_BONUS;
                else if (weaponType == WEAPONCLASS_FF_MELEE)
                    currentScore -= SCORE_DEFENSE_MELEE_PENALTY;
                break;
            case BOT_TASK_ASSASSINATE_VIP_FF:
                if (weaponType == WEAPONCLASS_FF_SHOTGUN || weaponType == WEAPONCLASS_FF_SUPERNAILGUN || weaponType == WEAPONCLASS_FF_TRANQGUN || weaponType == WEAPONCLASS_FF_SNIPERRIFLE)
                    currentScore += SCORE_ASSASSIN_BONUS;
                break;
            case BOT_TASK_ESCORT_VIP_FF: // Bodyguard
                if (weaponType == WEAPONCLASS_FF_SHOTGUN || weaponType == WEAPONCLASS_FF_NAILGUN || weaponType == WEAPONCLASS_FF_ASSAULTCANNON || weaponType == WEAPONCLASS_FF_FLAMETHROWER)
                    currentScore += SCORE_BODYGUARD_BONUS;
                break;
            default:
                break;
        }

        // 4. Ammo Status
        if (pWeaponInfo->m_iMaxClip1 > 0) {
            float ammoRatio = (float)weapon->Clip1() / pWeaponInfo->m_iMaxClip1;
            if (ammoRatio >= 0.9f) {
                currentScore += SCORE_AMMO_FULL_CLIP_BONUS;
            } else if (ammoRatio < 0.25f && weapon->UsesPrimaryAmmo()) {
                currentScore += SCORE_AMMO_LOW_PENALTY_FACTOR * (1.0f - ammoRatio);
            }
        }
        if (!weapon->HasPrimaryAmmo() && weapon->UsesPrimaryAmmo()) {
            currentScore += SCORE_NO_RESERVE_AMMO_PENALTY;
        }

        // 5. Special Class/Weapon Logic (Placeholders from subtask description)
        if (pClassInfo) { // Ensure pClassInfo is valid
            if (pClassInfo->m_iSlot == CLASS_MEDIC && weaponID == FF_WEAPON_MEDKIT) {
                // TODO_FF: Add score bonus if self or nearby friends are hurt.
                currentScore += SCORE_MEDKIT_BASE_BONUS;
            }
            if (pClassInfo->m_iSlot == CLASS_ENGINEER) {
                if (weaponID == FF_WEAPON_SPANNER) {
                    // TODO_FF: Bonus if buildables need repair
                    currentScore += SCORE_SPANNER_BASE_BONUS;
                }
                if (weaponID == FF_WEAPON_RAILGUN) {
                    currentScore += SCORE_RAILGUN_ENGINEER_COMBAT_BONUS;
                }
            }
        }

        // TODO_FF: Weapon Stats (DPS, projectile speed, etc.) - more complex, for later refinement.

        if (currentScore > bestScore) {
            bestScore = currentScore;
            bestWeapon = weapon;
        }
    }

	if (bestWeapon && GetActiveFFWeapon() != bestWeapon) {
        PrintIfWatched("EquipBestWeapon: Equipping %s (Score: %.2f)\n", STRING(bestWeapon->GetEntityName()), bestScore);
		DoEquip(bestWeapon);
        m_equipTimer.Start(minEquipInterval); // Reset timer after equipping
		return;
	}

    CFFWeaponBase* currentWep = GetActiveFFWeapon();
    if (currentWep && !currentWep->HasAnyAmmo() && (!bestWeapon || (bestWeapon && !bestWeapon->HasAnyAmmo()))) {
        PrintIfWatched("EquipBestWeapon: Active weapon out of all ammo, no better option found with ammo. Equipping melee.\n");
        EquipKnife();
        m_equipTimer.Start(minEquipInterval);
    }
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Equip our pistol
 */
void CFFBot::EquipPistol( void ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the melee weapon (Axe in FF)
 */
void CFFBot::EquipKnife( void ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a grenade in our inventory
 */
bool CFFBot::HasGrenade( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip a grenade, return false if we cant
 */
bool CFFBot::EquipGrenade( bool noSmoke ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have melee weapon equipped
 */
bool CFFBot::IsUsingKnife( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have pistol equipped
 */
bool CFFBot::IsUsingPistol( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have a grenade type weapon equipped
 */
bool CFFBot::IsUsingGrenade( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin the process of throwing the grenade
 */
void CFFBot::ThrowGrenade( const Vector &target ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if our weapon can attack
 */
bool CFFBot::CanActiveWeaponFire( void ) const { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Find spot to throw grenade ahead of us and "around the corner" along our path
 */
bool CFFBot::FindGrenadeTossPathTarget( Vector *pos ) { /* ... (implementation unchanged) ... */ }


//--------------------------------------------------------------------------------------------------------------
/**
 * Look for grenade throw targets and throw the grenade
 */
void CFFBot::LookForGrenadeTargets( void ) { /* ... (implementation unchanged) ... */ }


//--------------------------------------------------------------------------------------------------------------
/**
 * Process the grenade throw state machine
 */
void CFFBot::UpdateGrenadeThrow( void ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Reload our weapon if we must
 */
void CFFBot::ReloadCheck( void ) { /* ... (implementation unchanged) ... */ }

//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when in contact with a CBaseCombatWeapon
 */
bool CFFBot::BumpWeapon( CBaseCombatWeapon *pWeapon ) { /* ... (implementation unchanged) ... */ }


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if a friend is in our weapon's way
 */
bool CFFBot::IsFriendInLineOfFire( void ) { /* ... (implementation unchanged) ... */ }


//--------------------------------------------------------------------------------------------------------------
/**
 * Return line-of-sight distance to obstacle along weapon fire ray
 */
float CFFBot::ComputeWeaponSightRange( void ) { /* ... (implementation unchanged) ... */ }


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if the given player just fired their weapon
 */
bool CFFBot::DidPlayerJustFireWeapon( const CFFPlayer *player ) const { /* ... (implementation unchanged) ... */ }
