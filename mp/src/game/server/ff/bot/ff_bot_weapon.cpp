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
#include "../../shared/ff/ff_playerclass_parse.h" // For CFFPlayerClassInfo (needed for EquipBestWeapon)
#include "ff_gamestate.h"
#include "bot_constants.h"
#include "bot_profile.h"
#include "bot_util.h"
#include "ff_bot_weapon_id.h" // For GetWeaponClassTypeFF and FFWeaponID

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Fire our active weapon towards our current enemy
 * NOTE: Aiming our weapon is handled in RunBotUpkeep() -> CFFBot::Upkeep()
 */
void CFFBot::FireWeaponAtEnemy( void )
{
	if (cv_bot_dont_shoot.GetBool())
	{
		return;
	}

	CFFPlayer *enemy = GetBotEnemy(); // Changed CBasePlayer to CFFPlayer
	if (enemy == NULL)
	{
		return;
	}

	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (!weapon)
	{
		return;
	}
	const CFFWeaponInfo *pWeaponInfo = weapon->GetFFWpnData();
	if (!pWeaponInfo)
	{
		return;
	}

	// Sniper rifle specific logic
	if (GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_SNIPERRIFLE)
	{
		// TODO_FF: Implement FF specific sniper logic (zoom levels, charge time if any)
		// For now, basic steady check:
		if (!IsNotMoving() || IsWaitingForZoom() || (GetProfile() && !HasViewBeenSteady( GetProfile()->GetReactionTime() )) )
		{
			return;
		}
	}

	if (gpGlobals->curtime > m_fireWeaponTimestamp &&
		(!GetProfile() || GetTimeSinceAcquiredCurrentEnemy() >= GetProfile()->GetAttackDelay()) &&
		!IsSurprised())
	{
		if (!IsReloading() &&
			weapon->Clip1() > 0 && // Check current weapon's clip directly
			IsEnemyVisible())
		{
			Vector toAimSpot = m_aimSpot - EyePosition();
			float rangeToEnemy = toAimSpot.NormalizeInPlace();

			Vector aimDir = GetViewVector();
			float onTarget = DotProduct( toAimSpot, aimDir );

			// TODO_FF: Tune aimTolerance based on FF weapon characteristics
			const float halfSize = HalfHumanWidth; // Standard human width, adjust if needed
			float aimTolerance = (rangeToEnemy > FLT_EPSILON) ? (float)cos( atan( halfSize / rangeToEnemy ) ) : 1.0f;

			if (onTarget > aimTolerance)
			{
				bool doAttack = true;
				if (TheFFBots() && TheFFBots()->AllowFriendlyFireDamage())
				{
					if (IsFriendInLineOfFire())
						doAttack = false;
				}

				if (doAttack)
				{
					PrimaryAttack();
					// Use weapon's cycle time for next fire timestamp
					m_fireWeaponTimestamp = gpGlobals->curtime + pWeaponInfo->m_flCycleTime;
				}
				// Removed old CS-specific spray/burst logic for m_fireWeaponTimestamp
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Set the current aim offset using given accuracy (1.0 = perfect aim, 0.0f = terrible aim)
 */
void CFFBot::SetAimOffset( float accuracy )
{
	if (accuracy < 1.0f)
	{
		if (IsViewMoving( 100.0f ))
			m_aimSpreadTimestamp = gpGlobals->curtime;

		const float focusTime = MAX( 5.0f * (1.0f - accuracy), 2.0f );
		float focusInterval = gpGlobals->curtime - m_aimSpreadTimestamp;
		float focusAccuracy = (focusTime > FLT_EPSILON) ? (focusInterval / focusTime) : 1.0f;
		const float maxFocusAccuracy = 0.75f;
		if (focusAccuracy > maxFocusAccuracy) focusAccuracy = maxFocusAccuracy;
		accuracy = MAX( accuracy, focusAccuracy );
	}

	float range = (m_lastEnemyPosition - EyePosition()).Length();
	float maxOffset = (GetFOV()/GetDefaultFOV()) * 0.05f * range;
	float error = maxOffset * (1.0f - accuracy);

	m_aimOffsetGoal.x = RandomFloat( -error, error );
	m_aimOffsetGoal.y = RandomFloat( -error, error );
	m_aimOffsetGoal.z = RandomFloat( -error, error );
	m_aimOffsetTimestamp = gpGlobals->curtime + RandomFloat( 0.25f, 1.0f );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Wiggle aim error based on GetProfile()->GetSkill()
 */
void CFFBot::UpdateAimOffset( void )
{
	if (gpGlobals->curtime >= m_aimOffsetTimestamp)
	{
		if (GetProfile()) SetAimOffset( GetProfile()->GetSkill() );
	}
	Vector d = m_aimOffsetGoal - m_aimOffset;
	const float stiffness = 0.1f;
	m_aimOffset.x += stiffness * d.x;
	m_aimOffset.y += stiffness * d.y;
	m_aimOffset.z += stiffness * d.z;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Change our zoom level to be appropriate for the given range.
 * Return true if the zoom level changed.
 */
bool CFFBot::AdjustZoom( float range )
{
	// TODO_FF: Update for FF sniper/zoom logic. This is CS-like.
	// Needs to check current weapon's zoom capabilities from CFFWeaponInfo.
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (!weapon || GetWeaponClassTypeFF(weapon->GetWeaponID()) != WEAPONCLASS_FF_SNIPERRIFLE)
	{
		if (GetZoomLevel() != NO_ZOOM) // Assuming NO_ZOOM is 0
		{
			SecondaryAttack(); // Send zoom command to unzoom
			m_zoomTimer.Start(0.25f); // Generic unzoom time
			return true;
		}
		return false;
	}

	// Example: if FF sniper has multiple zoom levels defined in CFFWeaponInfo
	// const CFFWeaponInfo *pWeaponInfo = weapon->GetFFWpnData();
	// if (!pWeaponInfo) return false;
	// float zoomLevel1Range = pWeaponInfo->m_flZoomRange1; // Hypothetical
	// float zoomLevel2Range = pWeaponInfo->m_flZoomRange2; // Hypothetical

	bool adjustZoom = false;
	const float sniperZoomRange = 150.0f;	// Placeholder
	const float sniperFarZoomRange = 1500.0f; // Placeholder

	if (range <= sniperZoomRange) { if (GetZoomLevel() != NO_ZOOM) adjustZoom = true; }
	else if (range < sniperFarZoomRange) { if (GetZoomLevel() != LOW_ZOOM) adjustZoom = true; } // Assuming LOW_ZOOM = 1
	else { if (GetZoomLevel() != HIGH_ZOOM) adjustZoom = true; } // Assuming HIGH_ZOOM = 2

	if (adjustZoom)
	{
		SecondaryAttack(); // This toggles zoom levels
		m_zoomTimer.Start( 0.25f + (GetProfile() ? (1.0f - GetProfile()->GetSkill()) : 0.5f) );
	}
	return adjustZoom;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using the specific weapon ID
 */
bool CFFBot::IsUsing( FFWeaponID weaponID ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;
	return (weapon->GetWeaponID() == weaponID);
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we are using a weapon with a removable silencer
 * FF generally does not have silencers.
 */
bool CFFBot::DoesActiveWeaponHaveSilencer( void ) const
{
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a sniper rifle
 */
bool CFFBot::IsUsingSniperRifle( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_SNIPERRIFLE)
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a sniper rifle in our inventory
 */
bool CFFBot::IsSniper( void ) const
{
	for (int i = 0; i < MAX_WEAPONS; ++i) // MAX_WEAPONS is from CBasePlayer
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_SNIPERRIFLE)
			return true;
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are actively sniping (moving to sniper spot or settled in)
 */
bool CFFBot::IsSniping( void ) const
{
	// Assuming TaskType enum has these values from ff_bot.h
	if (GetTask() == CFFBot::MOVE_TO_SNIPER_SPOT || GetTask() == CFFBot::SNIPING)
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a shotgun
 */
bool CFFBot::IsUsingShotgun( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_SHOTGUN)
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using an assault cannon (FF's "machinegun")
 */
bool CFFBot::IsUsingMachinegun( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_ASSAULTCANNON)
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if primary weapon doesn't exist or is totally out of ammo
 * This needs to be class-dependent for FF. For now, checks specific common primary types.
 */
bool CFFBot::IsPrimaryWeaponEmpty( void ) const
{
	// TODO_FF: Better primary weapon determination based on bot's class
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (!weapon) continue;

		WeaponClassTypeFF wct = GetWeaponClassTypeFF(weapon->GetWeaponID());
		if (wct == WEAPONCLASS_FF_ASSAULTRIFLE || wct == WEAPONCLASS_FF_SHOTGUN ||
			wct == WEAPONCLASS_FF_SNIPERRIFLE || wct == WEAPONCLASS_FF_ROCKETLAUNCHER ||
			wct == WEAPONCLASS_FF_ASSAULTCANNON || wct == WEAPONCLASS_FF_FLAMETHROWER ||
			wct == WEAPONCLASS_FF_NAILGUN || wct == WEAPONCLASS_FF_PIPEGUN)
		{
			if (weapon->HasAnyAmmo()) return false; // Found a primary with ammo
		}
	}
	return true; // No primary with ammo found
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if pistol doesn't exist or is totally out of ammo
 * FF has a single pistol generally.
 */
bool CFFBot::IsPistolEmpty( void ) const
{
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_PISTOL)
		{
			return !weapon->HasAnyAmmo();
		}
	}
	return true; // No pistol found
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the given item
 */
bool CFFBot::DoEquip( CFFWeaponBase *weapon )
{
	if (weapon == NULL || !weapon->HasAnyAmmo()) return false;
	// Only equip if it's not already the active weapon
	if (GetActiveWeapon() == weapon) return true;

	SelectItem( weapon->GetClassname() );
	m_equipTimer.Start(); // Start a timer to prevent rapid re-equipping
	return true;
}

const float minEquipInterval = 2.0f; // Reduced from 5.0f, bots might need to switch faster.


//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the best weapon we are carrying that has ammo
 */
void CFFBot::EquipBestWeapon( bool mustEquip )
{
	if (!mustEquip && m_equipTimer.GetElapsedTime() < minEquipInterval && GetActiveFFWeapon() != NULL) return;
	if (!TheFFBots()) return;

	// TODO_FF: Major refactor for FF weapon selection based on class, situation, enemy range, etc.
	// This is a placeholder implementation.

	CFFWeaponBase* bestWeapon = NULL;
	float bestScore = -1.0f;

	CFFPlayer* pOwner = dynamic_cast<CFFPlayer*>(this->GetCommander()); // Assuming CFFBot is owned by CFFPlayer
	if (!pOwner) return;

	const CFFPlayerClassInfo* pClassInfo = pOwner->GetPlayerClassInfo(); // Assumes CFFPlayer has this method
	if (!pClassInfo) return;

	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (!weapon || !weapon->HasAnyAmmo() || !weapon->CanDeploy()) continue;

		const CFFWeaponInfo* pWeaponInfo = weapon->GetFFWpnData();
		if (!pWeaponInfo) continue;

		// Basic check: is this weapon part of the class's typical loadout?
		bool isClassWeapon = false;
		for(int j=0; j < MAX_PLAYERCLASS_WEAPONS; ++j)
		{
			if (pClassInfo->m_aWeapons[j].m_iWeaponID != FF_WEAPON_NONE &&
				pClassInfo->m_aWeapons[j].m_iWeaponID == weapon->GetWeaponID())
			{
				isClassWeapon = true;
				break;
			}
		}
		if (!isClassWeapon && weapon->GetWeaponID() != FF_WEAPON_AXE && weapon->GetWeaponID() != FF_WEAPON_SPANNER) // Always allow axe/spanner
		{
			// If not a direct class weapon, maybe it's a pickup the bot is allowed to use?
			// For now, be restrictive.
			// continue;
		}

		float score = 1.0f; // Base score for having ammo and being deployable

		// TODO_FF: Add more sophisticated scoring based on:
		// - Enemy presence and range (GetBotEnemy(), m_lastEnemyPosition)
		// - Weapon class type (sniper for long, shotgun for close)
		// - Ammo count (prefer more ammo)
		// - Bot's current task (IsSniping(), etc.)
		// - Weapon damage potential (pWeaponInfo->m_flDamage)

		WeaponClassTypeFF wct = GetWeaponClassTypeFF(weapon->GetWeaponID());

		if (GetBotEnemy())
		{
			float distToEnemySqr = (GetBotEnemy()->GetAbsOrigin() - GetAbsOrigin()).LengthSqr();
			if (wct == WEAPONCLASS_FF_SHOTGUN || wct == WEAPONCLASS_FF_FLAMETHROWER)
				score += (distToEnemySqr < Square(600.0f)) ? 100.0f : -50.0f; // Prefer at close range
			else if (wct == WEAPONCLASS_FF_SNIPERRIFLE)
				score += (distToEnemySqr > Square(1000.0f)) ? 100.0f : -50.0f; // Prefer at long range
			else if (wct == WEAPONCLASS_FF_ROCKETLAUNCHER || wct == WEAPONCLASS_FF_PIPEGUN || wct == WEAPONCLASS_FF_GRENADELAUNCHER)
				score += (distToEnemySqr > Square(400.0f) && distToEnemySqr < Square(2000.0f)) ? 80.0f : 0.0f; // Prefer at mid range
			else if (wct == WEAPONCLASS_FF_ASSAULTRIFLE || wct == WEAPONCLASS_FF_NAILGUN || wct == WEAPONCLASS_FF_ASSAULTCANNON)
				score += 50.0f; // General purpose
			else if (wct == WEAPONCLASS_FF_PISTOL)
				score += 10.0f;
		}
		else // No enemy
		{
			if (wct == WEAPONCLASS_FF_ASSAULTRIFLE || wct == WEAPONCLASS_FF_NAILGUN) score += 50.0f; // Good general purpose
			else if (wct == WEAPONCLASS_FF_PISTOL) score += 20.0f;
		}

		// Prefer weapons with more relative ammo in clip
		if (pWeaponInfo->m_iMaxClip1 > 0)
		{
			score += 10.0f * ((float)weapon->Clip1() / pWeaponInfo->m_iMaxClip1);
		}


		if (score > bestScore)
		{
			bestScore = score;
			bestWeapon = weapon;
		}
	}

	if (bestWeapon)
	{
		DoEquip(bestWeapon);
		return;
	}

	// If all else fails, equip Axe (or default FF melee)
	EquipKnife();
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip our pistol
 */
void CFFBot::EquipPistol( void )
{
	if (m_equipTimer.GetElapsedTime() < minEquipInterval && GetActiveFFWeapon() != NULL && IsUsingPistol()) return;

	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_PISTOL)
		{
			DoEquip(weapon);
			return;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the melee weapon (Axe in FF)
 */
void CFFBot::EquipKnife( void )
{
	if (IsUsingKnife()) return;

	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		// Assuming FF_WEAPON_AXE is the primary melee weapon ID
		if (weapon && weapon->GetWeaponID() == FF_WEAPON_AXE)
		{
			DoEquip(weapon);
			return;
		}
	}
	// Fallback: Try to equip any melee weapon if Axe not found by ID
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_MELEE)
		{
			DoEquip(weapon);
			return;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a grenade in our inventory
 * FF has various grenade types, check for any usable grenade weapon.
 */
bool CFFBot::HasGrenade( void ) const
{
	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (!weapon) continue;
		WeaponClassTypeFF wct = GetWeaponClassTypeFF(weapon->GetWeaponID());
		// TODO_FF: Define what constitutes a "grenade" weapon class for bots to use with this function.
		// This could be hand grenades, or weapons that launch grenade-like projectiles.
		// For now, let's assume it means hand grenades.
		if (wct == WEAPONCLASS_FF_HANDGRENADE || wct == WEAPONCLASS_FF_NAILGRENADE || wct == WEAPONCLASS_FF_MIRVGRENADE || wct == WEAPONCLASS_FF_EMPGRENADE || wct == WEAPONCLASS_FF_GASGRENADE)
		{
			if (weapon->HasAnyAmmo()) return true;
		}
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip a grenade, return false if we cant
 */
bool CFFBot::EquipGrenade( bool noSmoke /* FF doesn't have smoke in the same CS way, param might be repurposed */ )
{
	// TODO_FF: More nuanced grenade selection based on 'noSmoke' or other criteria.
	// For now, just equip any available hand grenade.
	if (IsUsingGrenade() && GetActiveFFWeapon() && GetActiveFFWeapon()->HasAnyAmmo()) return true; // Already using a grenade with ammo

	for (int i = 0; i < MAX_WEAPONS; ++i)
	{
		CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(GetWeapon(i));
		if (!weapon || !weapon->HasAnyAmmo()) continue;

		WeaponClassTypeFF wct = GetWeaponClassTypeFF(weapon->GetWeaponID());
		if (wct == WEAPONCLASS_FF_HANDGRENADE || wct == WEAPONCLASS_FF_NAILGRENADE || wct == WEAPONCLASS_FF_MIRVGRENADE || wct == WEAPONCLASS_FF_EMPGRENADE || wct == WEAPONCLASS_FF_GASGRENADE)
		{
			// Example: if (noSmoke && wct == WEAPONCLASS_FF_GASGRENADE) continue; // If gas grenade is like "smoke"
			DoEquip(weapon);
			return true;
		}
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have melee weapon equipped
 */
bool CFFBot::IsUsingKnife( void ) const // Renamed from IsUsingAxe for consistency, but refers to melee
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_MELEE)
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have pistol equipped
 */
bool CFFBot::IsUsingPistol( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon && GetWeaponClassTypeFF(weapon->GetWeaponID()) == WEAPONCLASS_FF_PISTOL)
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have a grenade type weapon equipped
 */
bool CFFBot::IsUsingGrenade( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (!weapon) return false;
	WeaponClassTypeFF wct = GetWeaponClassTypeFF(weapon->GetWeaponID());
	if (wct == WEAPONCLASS_FF_HANDGRENADE || wct == WEAPONCLASS_FF_NAILGRENADE || wct == WEAPONCLASS_FF_MIRVGRENADE || wct == WEAPONCLASS_FF_EMPGRENADE || wct == WEAPONCLASS_FF_GASGRENADE ||
		wct == WEAPONCLASS_FF_GRENADELAUNCHER || wct == WEAPONCLASS_FF_PIPEGUN ) // Pipegun also launches grenade-like things
		return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin the process of throwing the grenade
 * TODO_FF: Actual grenade throwing logic will be complex (aiming, timing).
 */
void CFFBot::ThrowGrenade( const Vector &target )
{
	if (IsUsingGrenade() && m_grenadeTossState == NOT_THROWING && !IsOnLadder())
	{
		// For hand grenades, need to start primary attack (pull pin) then release (throw)
		// For launchers (GL, Pipe), just PrimaryAttack()
		CFFWeaponBase* activeWeapon = GetActiveFFWeapon();
		if (!activeWeapon) return;

		WeaponClassTypeFF wct = GetWeaponClassTypeFF(activeWeapon->GetWeaponID());
		if (wct == WEAPONCLASS_FF_HANDGRENADE || wct == WEAPONCLASS_FF_NAILGRENADE || wct == WEAPONCLASS_FF_MIRVGRENADE || wct == WEAPONCLASS_FF_EMPGRENADE || wct == WEAPONCLASS_FF_GASGRENADE)
		{
			// This is simplified. Real hand grenade throwing involves holding attack, then releasing.
			// Bot AI might need a state machine for this.
			// For now, just press and assume it throws quickly or the bot handles the hold.
			PrimaryAttack();
			m_grenadeTossState = THROWING_GRENADE; // Set a state
			m_tossGrenadeTimer.Start(1.0f); // Cooldown/timer for state
		}
		else if (wct == WEAPONCLASS_FF_GRENADELAUNCHER || wct == WEAPONCLASS_FF_PIPEGUN)
		{
			PrimaryAttack();
			// No complex state needed for direct launchers usually
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if our weapon can attack
 */
bool CFFBot::CanActiveWeaponFire( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	return ( weapon && weapon->m_flNextPrimaryAttack <= gpGlobals->curtime );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Find spot to throw grenade ahead of us and "around the corner" along our path
 * TODO_FF: This is complex path-aware AI, for now, return false.
 */
bool CFFBot::FindGrenadeTossPathTarget( Vector *pos )
{
	if (!pos || !HasPath()) return false;
	// Placeholder: More advanced logic would inspect the path for corners, cover, enemies.
	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Look for grenade throw targets and throw the grenade
 * TODO_FF: Grenade targeting logic needed.
 */
void CFFBot::LookForGrenadeTargets( void )
{
	if (!IsUsingGrenade() || IsThrowingGrenade()) return;
	// Placeholder: Needs logic to decide when and where to throw.
	// Could be based on enemy clusters, enemy behind cover, etc.
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Process the grenade throw state machine
 */
void CFFBot::UpdateGrenadeThrow( void )
{
	if (m_grenadeTossState == THROWING_GRENADE)
	{
		if (m_tossGrenadeTimer.IsElapsed())
		{
			m_grenadeTossState = NOT_THROWING;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Reload our weapon if we must
 */
void CFFBot::ReloadCheck( void )
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (!weapon || !weapon->CanReload() || weapon->IsReloading()) return; // Already reloading or cannot reload

	// if (IsDefusingBomb()) return; // No equivalent in FF directly, maybe capturing a point?

	bool needsReload = false;
	if (weapon->Clip1() <= 0 && weapon->HasAnyAmmo()) // Clip empty but has reserve
	{
		needsReload = true;
	}
	else
	{
		const float safeReloadWaitTime = 3.0f;
		const float reloadAmmoRatio = 0.6f; // Reload if clip is below 60% AND it's safe
		const CFFWeaponInfo *pWeaponInfo = weapon->GetFFWpnData();

		if (pWeaponInfo && pWeaponInfo->m_iMaxClip1 > 0 &&
			(float)weapon->Clip1() / pWeaponInfo->m_iMaxClip1 <= reloadAmmoRatio)
		{
			if (GetTimeSinceLastSawEnemy() > safeReloadWaitTime || GetEnemiesRemaining() == 0)
			{
				// Don't interrupt attacks for tactical reloads if skilled and fighting
				if (GetProfile() && GetProfile()->GetSkill() > 0.5f && IsAttacking() && IsEnemyVisible())
				{
					// Skilled bot might hold off tactical reload if actively engaging
				}
				else
				{
					needsReload = true;
				}
			}
		}
	}

	if (!needsReload) return;

	Reload(); // This is CBaseCombatCharacter::Reload()

	// TODO_FF: Consider if retreating to reload is common FF bot behavior.
	// The CS logic for retreating to reload based on enemy count might be too specific.
	// For now, rely on the reload happening and bot continuing other behaviors.
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when in contact with a CBaseCombatWeapon
 * TODO_FF: Adapt for FF item pickup logic. FF might not use "slots" in the same way.
 * This simplified version just checks if the bot can pick it up.
 */
bool CFFBot::BumpWeapon( CBaseCombatWeapon *pWeapon )
{
	CFFWeaponBase *pFFWeapon = dynamic_cast<CFFWeaponBase*>(pWeapon);
	if (pFFWeapon)
	{
		// Standard player weapon bumping logic will handle most cases (CanPickupWeapon)
		return BaseClass::BumpWeapon(pWeapon);
	}
	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if a friend is in our weapon's way
 */
bool CFFBot::IsFriendInLineOfFire( void )
{
	Vector aimDir = GetViewVector();
	trace_t result;
	// MASK_PLAYERSOLID might be too restrictive if FF has different collision for players/projectiles.
	// MASK_SHOT_HULL or MASK_SHOT might be better depending on weapon.
	UTIL_TraceLine( EyePosition(), EyePosition() + BotDefaultVisibleDistance, MASK_PLAYERSOLID, this, COLLISION_GROUP_PLAYER, &result );

	if (result.DidHitNonWorldEntity() && result.m_pEnt)
	{
		CBaseEntity *victim = result.m_pEnt;
		// Must be a player and alive
		if (victim->IsPlayer() && victim->IsAlive())
		{
			// Must be on the same team
			if (victim->GetTeamNumber() == GetTeamNumber())
			{
				// Don't shoot self (should be handled by trace filter, but double check)
				if (victim == static_cast<CBaseEntity*>(this)) return false;
				return true;
			}
		}
	}
	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return line-of-sight distance to obstacle along weapon fire ray
 */
float CFFBot::ComputeWeaponSightRange( void )
{
	Vector aimDir = GetViewVector();
	trace_t result;
	// Using MASK_SHOT_HULL as a general mask for what blocks weapon fire.
	UTIL_TraceLine( EyePosition(), EyePosition() + BotDefaultVisibleDistance, MASK_SHOT_HULL, this, COLLISION_GROUP_NONE, &result );
	return (result.endpos - EyePosition()).Length();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if the given player just fired their weapon
 */
bool CFFBot::DidPlayerJustFireWeapon( const CFFPlayer *player ) const
{
	if (!player) return false;
	CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(player->GetActiveWeapon());
	// FF doesn't have silencers, so that check is removed.
	// Check if the weapon's next primary attack time is in the future, indicating it was just fired.
	return (weapon && weapon->m_flNextPrimaryAttack > gpGlobals->curtime);
}

[end of mp/src/game/server/ff/bot/ff_bot_weapon.cpp]
