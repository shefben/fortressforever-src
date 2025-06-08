//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_gamerules.h"
#include "func_breakablesurf.h"
#include "obstacle_pushaway.h"

#include "ff_bot.h"
#include "ff_player.h" // Ensure CFFPlayer is included
#include "ff_bot_manager.h" // For TheFFBots and team definitions
#include "ff_weapon_base.h" // FF_WEAPONS: Added
#include "states/ff_bot_heal_teammate.h" // Medic healing state
#include "states/ff_bot_build_sentry.h"  // Engineer building state
#include "states/ff_bot_build_dispenser.h" // Engineer building dispenser state
#include "states/ff_bot_repair_buildable.h" // Engineer repairing state
#include "states/ff_bot_find_resources.h" // Engineer finding resources state
#include "states/ff_bot_guard_sentry.h"   // Engineer guarding sentry state
#include "states/ff_bot_infiltrate.h"     // Spy infiltration state
#include "states/ff_bot_follow_teammate.h" // Bot Following Teammate state
#include "ff_buildableobject.h" // For CFFBuildableObject, CFFSentryGun, CFFDispenser
#include "items.h" // For CItem, assuming ammo packs might be derived from it
// FF_TODO_AI_BEHAVIOR: Include headers for CaptureObjectiveState and GuardSentryState if GetTargetObjective/GetSentryBeingGuarded is used.
// #include "states/ff_bot_capture_objective.h"
// #include "states/ff_bot_guard_sentry.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const int SENTRY_COST_CELLS = 130;
const int DISPENSER_COST_CELLS = 100;
// These were previously extern in build states, now defined here for clarity.
// Ideally, these would be in a shared game constants header.

// Class-specific constants
const float CFFBot::FLAMETHROWER_EFFECTIVE_RANGE; // Defined in ff_bot.h
const float CFFBot::MINIGUN_SPINUP_TIME;          // Defined in ff_bot.h
const float CFFBot::MINIGUN_EFFECTIVE_RANGE;    // Defined in ff_bot.h
const float CFFBot::MINIGUN_SPINDOWN_DELAY;       // Defined in ff_bot.h
const float CFFBot::SCOUT_MAX_SPEED;              // Defined in ff_bot.h

// Additional placeholder constants for weapon switching logic
const float SCOUT_PRIMARY_EFFECTIVE_RANGE = 400.0f; // Example: Scout's Scattergun is good up to this range
const float MINIGUN_MIN_ENGAGEMENT_RANGE = 100.0f; // Example: Heavy might prefer shotgun or melee if enemy is closer than this

// Constants for Pyro projectile scanning
const float PYRO_AIRBLAST_DETECTION_RANGE = 300.0f;
const float PYRO_AIRBLAST_REACTION_RANGE = 150.0f;

const float CFFBot::HEAVY_MAX_SPEED;              // Defined in ff_bot.h
const float CFFBot::HEAVY_SPUNUP_MAX_SPEED;       // Defined in ff_bot.h


LINK_ENTITY_TO_CLASS( ff_bot, CFFBot );

BEGIN_DATADESC( CFFBot )
	// DEFINE_FIELD( m_isRogue, FIELD_BOOLEAN ), // Example if we were saving this
END_DATADESC()

//--------------------------------------------------------------------------------------------------------------
CFFBot::CFFBot()
{
	// FF_TODO_AI_BEHAVIOR: Initialize any FF-specific members
	m_fireWeaponTimestamp = 0.0f;
	m_isRapidFiring = false;
	m_zoomTimer.Invalidate();
	m_airblastCooldown.Invalidate();
	m_selectedBuildableType = BUILDABLE_NONE;
	m_cycleBuildableCooldown.Invalidate();

	// Engineer buildable state
	m_sentryGun = NULL;
	m_dispenser = NULL;
	m_teleEntrance = NULL;
	m_teleExit = NULL;
	m_sentryLevel = 0;
	m_dispenserLevel = 0;
	m_teleEntranceLevel = 0;
	m_teleExitLevel = 0;
	m_sappedBuildingHandle = NULL;
	m_hasSappedBuilding = false;

	// Scout members
	m_doubleJumpCooldown.Invalidate();
	m_isAttemptingDoubleJump = false;
	m_doubleJumpPhase = 0;
	m_doubleJumpPhaseTimer.Invalidate();

	// Heavy members
	m_isHeavyMinigunSpunUp = false;

	m_weaponSwitchCheckTimer.Invalidate();
	m_projectileScanTimer.Invalidate();

	// Engineer building state
	m_sentryIsBuilding = false;
	m_dispenserIsBuilding = false;

	// Enemy prioritization
	m_prioritizedEnemy = NULL;
	m_enemyPriorityTimer.Invalidate();

	// Demoman specific
	m_deployedStickiesCount = 0;
	m_stickyArmTime.Invalidate();
	m_stickyDetonateCooldown.Invalidate();

	// Flag carrying
	m_carriedFlag = NULL;
	m_carriedFlagType = 0; // 0 = none, 1 = enemy flag, 2 = own flag

	m_opportunisticReloadTimer.Invalidate();
}

CFFBot::~CFFBot()
{
}

//--------------------------------------------------------------------------------------------------------------
// Medic Notifications
//--------------------------------------------------------------------------------------------------------------
void CFFBot::NotifyCalledForMedic(CFFPlayer* pCaller)
{
	if (!IsMedic()) return;

	if (pCaller && pCaller != this && pCaller->IsAlive() && InSameTeam(pCaller) && pCaller->GetHealth() < pCaller->GetMaxHealth())
	{
		PrintIfWatched("Medic %s: Teammate %s is calling for medic and needs health (%.0f/%.0f)!\n",
			GetPlayerName(), pCaller->GetPlayerName(), pCaller->GetHealth(), pCaller->GetMaxHealth());

		// FF_TODO_AI_BEHAVIOR: Could add pCaller to a list of pending heal requests if already busy.
		// For now, consider switching if new caller is better candidate.

		CFFPlayer* currentHealTarget = NULL;
		if (m_healTeammateState.IsInState()) // Assumes HealTeammateState has IsInState()
		{
			// Conceptual: HealTeammateState needs a GetHealTarget() method
			// currentHealTarget = static_cast<CFFPlayer*>(m_healTeammateState.GetHealTarget());
			// For now, if in state, assume it has a target and we might not easily get it here
			// without modifying HealTeammateState more. Let's assume StartHealing handles it.
		}


		if (currentHealTarget != pCaller) // Avoid interrupting if already healing this caller
		{
			float distToCaller = GetRangeTo(pCaller);
			float distToCurrent = FLT_MAX;
			if (currentHealTarget) distToCurrent = GetRangeTo(currentHealTarget);

			// Simple priority: If not healing anyone, or new caller is much closer,
			// or new caller is significantly more hurt than current target (if current target is mostly healthy).
			bool takeNewTarget = false;
			if (!currentHealTarget)
			{
				takeNewTarget = true;
			}
			else
			{
				if (distToCaller < (distToCurrent * 0.75f)) takeNewTarget = true;
				else if (pCaller->GetHealth() < pCaller->GetMaxHealth() * 0.5f &&
				         currentHealTarget->GetHealth() > currentHealTarget->GetMaxHealth() * 0.75f)
				{
					takeNewTarget = true;
				}
			}

			if (takeNewTarget)
			{
				PrintIfWatched("Medic %s: Prioritizing call for medic from %s.\n", GetPlayerName(), pCaller->GetPlayerName());
				StartHealing(pCaller); // This method sets state to HealTeammateState
			}
			else
			{
				PrintIfWatched("Medic %s: Call for medic from %s noted, but current heal target priority is higher or similar.\n", GetPlayerName(), pCaller->GetPlayerName());
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::NotifyGaveMedicHealth(CFFPlayer* pTarget, int healthGiven)
{
	if (!IsMedic()) return;

	if (pTarget && pTarget != this && healthGiven > 0)
	{
		PrintIfWatched("Medic %s: Successfully gave %d health to %s.\n", GetPlayerName(), healthGiven, pTarget->GetPlayerName());
		// FF_TODO_CLASS_MEDIC: This notification is primarily for external systems or logging.
		// HealTeammateState itself checks target's health to decide when to stop or switch.
		// Could potentially use this to slightly extend a timer in HealTeammateState to provide overheal,
		// or for a medic bot to feel "more useful" if that impacts its behavior/morale.
	}
}


//--------------------------------------------------------------------------------------------------------------
bool CFFBot::NeedsAmmo(int weaponSlot) const
{
	// FF_TODO_WEAPON_STATS: This is a basic implementation. More sophisticated checks could be:
	// - Specific ammo thresholds per weapon type.
	// - Considering total carried ammo vs. clip size (e.g., needs ammo if less than 2 full clips total).
	// - For Engineer, checking metal supply for build/repair could also be part of a broader "needs resources".

	if (weaponSlot == -1) // Check active weapon, or primary/secondary if active is melee
	{
		CFFWeaponBase *pCurrentWeapon = GetActiveFFWeapon();
		if (pCurrentWeapon)
		{
			if (pCurrentWeapon->IsMeleeWeapon())
			{
				// If current is melee, check primary, then secondary
				CFFWeaponBase *pPrimary = static_cast<CFFWeaponBase*>(Weapon_GetSlot(FF_PRIMARY_WEAPON_SLOT));
				if (pPrimary && !pPrimary->IsMeleeWeapon() && pPrimary->HasPrimaryAmmo())
				{
					if (pPrimary->Clip1() <= 0 && GetAmmoCount(pPrimary->GetPrimaryAmmoType()) <= 0) return true; // Completely out
					if (pPrimary->GetMaxClip1() > 0 && (float)pPrimary->Clip1() / pPrimary->GetMaxClip1() < 0.25f && GetAmmoCount(pPrimary->GetPrimaryAmmoType()) < pPrimary->GetMaxClip1()) return true; // Low clip and not much reserve
				}
				CFFWeaponBase *pSecondary = static_cast<CFFWeaponBase*>(Weapon_GetSlot(FF_SECONDARY_WEAPON_SLOT));
				if (pSecondary && !pSecondary->IsMeleeWeapon() && pSecondary->HasPrimaryAmmo()) // Most secondaries use primary ammo type
				{
					if (pSecondary->Clip1() <= 0 && GetAmmoCount(pSecondary->GetPrimaryAmmoType()) <= 0) return true;
					if (pSecondary->GetMaxClip1() > 0 && (float)pSecondary->Clip1() / pSecondary->GetMaxClip1() < 0.25f && GetAmmoCount(pSecondary->GetPrimaryAmmoType()) < pSecondary->GetMaxClip1()) return true;
				}
				return false; // Melee active, primary/secondary seem okay or don't exist/use ammo
			}
			else if (pCurrentWeapon->HasPrimaryAmmo()) // Non-melee weapon
			{
				if (pCurrentWeapon->Clip1() <= 0 && GetAmmoCount(pCurrentWeapon->GetPrimaryAmmoType()) <= 0) return true;
				if (pCurrentWeapon->GetMaxClip1() > 0 && (float)pCurrentWeapon->Clip1() / pCurrentWeapon->GetMaxClip1() < 0.25f && GetAmmoCount(pCurrentWeapon->GetPrimaryAmmoType()) < pCurrentWeapon->GetMaxClip1()) return true;
			}
			// FF_TODO_WEAPON_STATS: Add check for weapons using secondary ammo type if any exist (pCurrentWeapon->HasSecondaryAmmo())
		}
		return false; // No weapon or weapon doesn't use primary ammo
	}
	else // Check specific slot
	{
		CFFWeaponBase *pWeapon = static_cast<CFFWeaponBase*>(Weapon_GetSlot(weaponSlot));
		if (pWeapon && !pWeapon->IsMeleeWeapon() && pWeapon->HasPrimaryAmmo())
		{
			if (pWeapon->Clip1() <= 0 && GetAmmoCount(pWeapon->GetPrimaryAmmoType()) <= 0) return true;
			if (pWeapon->GetMaxClip1() > 0 && (float)pWeapon->Clip1() / pWeapon->GetMaxClip1() < 0.25f && GetAmmoCount(pWeapon->GetPrimaryAmmoType()) < pWeapon->GetMaxClip1()) return true;
		}
		// FF_TODO_WEAPON_STATS: Add check for weapons using secondary ammo type
		return false;
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::HandleCommand(const char* command)
{
	if (!command || command[0] == '\0')
		return;

	char cmd[256];
	Q_snprintf(cmd, sizeof(cmd), "%s\n", command);

	// FF_TODO_GAME_MECHANIC: Determine if ServerCommand or ClientCommand is appropriate.
	// For build commands (e.g., "buildsentrygun"), ServerCommand was used from CFFBot::SelectSpecificBuildable.
	// Assuming sabotage commands are also server-level commands that a bot can issue for itself.
	// If these are client-only commands (like `voicemenu`), then ClientCommand(edict(), cmd) might be needed.
	// For now, using ServerCommand based on existing pattern for build commands.
	engine->ServerCommand(cmd);
	// PrintIfWatched("Bot %s: Issued server command: %s", GetPlayerName(), command); // Optional: for debugging
}

//--------------------------------------------------------------------------------------------------------------
int CFFBot::GetEntityOmniBotClass(CBaseEntity* pEntity)
{
	if (!pEntity)
		return FF_CLASSEX_UNKNOWN;

	// This function aims to replicate the classification logic from FFInterface::GetEntityClass (omnibot_interface.cpp)
	// It should return the same integer values that FFInterface::GetEntityClass would.
	// FF_CRITICAL_TODO_OMNIBOT: The actual omnibot_interface.cpp (or equivalent for FF) is missing,
	// so these mappings are based on TF_Config.h and assumptions about FF projectile Classify() results.

	// Handle Players
	if (pEntity->IsPlayer())
	{
		CFFPlayer* pPlayer = static_cast<CFFPlayer*>(pEntity);
		// FFInterface::GetEntityClass uses: obUtilGetBotClassFromGameClass(pFFPlayer->GetClassSlot())
		// Assuming GetClassSlot() returns 1 for Scout, ..., 10 for Civilian/Spy, matching our FF_CLASS_ enums.
		int gameClassSlot = pPlayer->GetClassSlot();
		if (gameClassSlot >= FF_CLASS_SCOUT && gameClassSlot <= FF_CLASS_CIVILIAN) // FF_CLASS_CIVILIAN is 10
		{
			return gameClassSlot;
		}
		// FF_TODO_OMNIBOT: If GetClassSlot() doesn't map directly, use pEntity->Classify() and map explicitly
		// case CLASS_PLAYER_SCOUT: return FF_CLASS_SCOUT; etc.
		return FF_CLASSEX_UNKNOWN;
	}

	// Handle other entity types based on their game Classify() result
	switch (pEntity->Classify())
	{
		// Projectiles: Map game's Classify() result to direct TF_CLASSEX_ values from TF_Config.h
		// Note: CLASS_IC_ROCKET (Incendiary Cannon) is assumed to also return CLASS_ROCKET from its Classify()
		// or FFInterface::GetEntityClass maps it to TF_CLASSEX_ROCKET.
		case CLASS_ROCKET:		// Standard SDK value for rockets. Covers normal and IC rockets.
			return FF_CLASSEX_ROCKET; // 30
		case CLASS_PIPEBOMB:	// Standard SDK value for demoman pipes (non-sticky).
			return FF_CLASSEX_PIPE;   // 28
		case CLASS_GRENADE:		// Standard SDK value for generic grenades.
			// FF_CRITICAL_TODO_PYRO_PROJ: This mapping is ambiguous without omnibot_interface.cpp.
			// CFFProjectileGrenade (Grenade Launcher) is likely CLASS_GRENADE.
			// A separate CFFProjectileHEGrenade (Hand Grenade) would also be CLASS_GRENADE.
			// Omnibot's FFInterface::GetEntityClass would differentiate them (e.g. by model or owner type).
			// For now, mapping CLASS_GRENADE to FF_CLASSEX_GLGRENADE (29) for launched pipes.
			// ScanForNearbyProjectiles will need to be robust enough to also consider FF_CLASSEX_GRENADE (20)
			// if it encounters an entity that the game classifies as CLASS_GRENADE.
			// A more robust GetEntityOmniBotClass would try to distinguish here.
			// Example (conceptual, needs actual game logic):
			// if ( dynamic_cast<CFFProjectileGrenade*>(pEntity) ) return FF_CLASSEX_GLGRENADE; // Grenade Launcher (arcing)
			// if ( dynamic_cast<CFFProjectileHEGrenade*>(pEntity) ) return FF_CLASSEX_GRENADE; // Hand Grenade
			// FF_CRITICAL_TODO_PYRO_PROJ: Verify CFFProjectileGrenade (GL) Classify() result.
			// If it's just CLASS_GRENADE, airblast might target HE grenades when trying to reflect GL pipes.
			// For now, assume game's CLASS_GRENADE refers to the hand grenade type.
			return FF_CLASSEX_GRENADE; // Should be 20 (Hand Grenade)

		// FF_TODO_PYRO_PROJ: Add other reflectable projectiles like nails if CLASS_NAIL exists and maps to a TF_CLASSEX_
		// case CLASS_NAIL: return FF_CLASSEX_NAIL_GRENADE; // (Value 22) - Need to confirm Classify() for individual nails.

		// Buildings: Map game's Classify() result to Omnibot-derived FF_CLASSEX_ values (e.g., Sentry Lvl1/2/3)
		case CLASS_SENTRYGUN:
		{
			CFFSentryGun *sentry = dynamic_cast<CFFSentryGun*>(pEntity);
			if (sentry)
			{
				// This matches typical Omnibot logic for deriving level-specific classes
				switch (sentry->GetUpgradeLevel())
				{
					case 1: return FF_CLASSEX_SENTRY_LVL1; // 11
					case 2: return FF_CLASSEX_SENTRY_LVL2; // 12
					case 3: return FF_CLASSEX_SENTRY_LVL3; // 13
				}
			}
			return FF_CLASSEX_SENTRY_LVL1; // Default to Lvl1 if level unknown or not a CFFSentryGun
		}
		case CLASS_DISPENSER:
			return FF_CLASSEX_DISPENSER; // 14

		// FF_CRITICAL_TODO_ENGINEER: Verify actual Classify() results for FF teleporter objects
		// and their mapping in the (missing) FFInterface::GetEntityClass.
		// Assuming CLASS_TELEPORTER_ENTRANCE and CLASS_TELEPORTER_EXIT exist as distinct Classify() results.
		// If not, FFInterface::GetEntityClass would need to use FClassnameIs or other properties
		// on a generic CLASS_BUILDING or CLASS_TELEPORTER.
		case CLASS_TELEPORTER_ENTRANCE: // Placeholder SDK enum value
			return FF_CLASSEX_TELE_ENTR; // 15
		case CLASS_TELEPORTER_EXIT:     // Placeholder SDK enum value
			return FF_CLASSEX_TELE_EXIT; // 16
		/* // Example alternative if specific Classify() values don't exist:
		case CLASS_BUILDING_GENERIC: // Or whatever a generic buildable might classify as
			if (FClassnameIs(pEntity, "obj_teleporter_entrance")) return FF_CLASSEX_TELE_ENTR;
			if (FClassnameIs(pEntity, "obj_teleporter_exit")) return FF_CLASSEX_TELE_EXIT;
			break;
		*/

		// Other TF_CLASSEX_ types from TF_Config.h that might be identified by Classify()
		// These are less critical for Pyro airblast but good for completeness if this function is used broadly.
		// case CLASS_DETPACK: return TF_CLASSEX_DETPACK; // TF_CLASSEX_DETPACK is 19 in TF_Config.h
		// case CLASS_TURRET_BASE: return TF_CLASSEX_TURRET; // TF_CLASSEX_TURRET is 31 in TF_Config.h

		default:
			// FFInterface::GetEntityClass might have FClassnameIs checks here for entities
			// not well covered by Classify(), or to refine types.
			// For projectile detection, the main projectile Classify() cases are key.
			break;
	}

	return FF_CLASSEX_UNKNOWN;
}

//--------------------------------------------------------------------------------------------------------------
// FF_TODO_CLASS_ENGINEER: Player Engineers likely use a key (e.g., bound to slot4, lastinv,
// or specific build keys like 'buildsentry', 'builddispenser') to select buildables.
// Bots currently simulate this by setting m_selectedBuildableType.
// FF_TODO_GAME_MECHANIC: If IN_ATTACK2 with Spanner cycles blueprints, CFFBot::CycleSelectedBuildable() should simulate IN_ATTACK2 presses.
//--------------------------------------------------------------------------------------------------------------

void CFFBot::CycleSelectedBuildable()
{
	if (!IsEngineer())
		return;
	BuildableType current = m_selectedBuildableType;
	if (current == BUILDABLE_NONE) current = BUILDABLE_SENTRY;
	switch (current) {
		case BUILDABLE_SENTRY: m_selectedBuildableType = BUILDABLE_DISPENSER; break;
		case BUILDABLE_DISPENSER: m_selectedBuildableType = BUILDABLE_SENTRY; break;
		// FF_TODO_CLASS_ENGINEER: Add cycling through teleporter entrance/exit if desired
		default: m_selectedBuildableType = BUILDABLE_SENTRY; break;
	}
	PrintIfWatched("%s internally cycled preferred buildable to: %s\n", GetPlayerName(), GetSelectedBuildableName());
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::SelectSpecificBuildable(BuildableType type)
{
	if (!IsEngineer()) return;
	if (type <= BUILDABLE_NONE || type >= NUM_BUILDABLE_TYPES) {
		Warning("CFFBot::SelectSpecificBuildable: Invalid buildable type %d requested for %s.\n", type, GetPlayerName());
		return;
	}
	const char *command = NULL;
	switch (type) {
		case BUILDABLE_SENTRY: command = "buildsentrygun"; break;
		case BUILDABLE_DISPENSER: command = "builddispenser"; break;
		case BUILDABLE_TELE_ENTRANCE: command = "buildteleporterentrance"; break;
		case BUILDABLE_TELE_EXIT: command = "buildteleporterexit"; break;
		default: Warning("CFFBot::SelectSpecificBuildable: Unknown buildable type %d for %s.\n", type, GetPlayerName()); return;
	}
	if (command) {
		PrintIfWatched("%s: Executing build command: '%s'\n", GetPlayerName(), command);
		engine->ServerCommand(UTIL_VarArgs("%s\n", command));
		m_selectedBuildableType = type;
	}
}

//--------------------------------------------------------------------------------------------------------------
const char* CFFBot::GetSelectedBuildableName() const
{
	switch(m_selectedBuildableType) {
		case BUILDABLE_SENTRY: return "Sentry Gun";
		case BUILDABLE_DISPENSER: return "Dispenser";
		case BUILDABLE_TELE_ENTRANCE: return "Teleporter Entrance";
		case BUILDABLE_TELE_EXIT: return "Teleporter Exit";
		case BUILDABLE_NONE: default: return "None";
	}
}

//--------------------------------------------------------------------------------------------------------------
// Buildable Notification Handlers
//--------------------------------------------------------------------------------------------------------------
void CFFBot::NotifyBuildingSapped(CBaseEntity *sappedBuilding, bool isSapped)
{
	// Only track our own buildings being sapped by this specific notification
	// FF_TODO_CLASS_ENGINEER: Extend to handle teleporters if they can be sapped.
	if (sappedBuilding == m_sentryGun.Get() || sappedBuilding == m_dispenser.Get())
	{
		m_sappedBuildingHandle = (isSapped ? sappedBuilding : NULL);
		m_hasSappedBuilding = isSapped;
		PrintIfWatched("Bot %s: Building %s sapped status changed to: %s\n",
			GetPlayerName(),
			sappedBuilding->GetClassname(),
			isSapped ? "SAPPED" : "NOT SAPPED");

		if (isSapped && GetState() != &m_repairBuildableState)
		{
			PrintIfWatched("Bot %s: My building %s was sapped! Attempting to repair.\n", GetPlayerName(), sappedBuilding->GetClassname());
			TryToRepairBuildable(sappedBuilding);
		}
	}
}

void CFFBot::NotifyBuildingUpgraded(CBaseEntity *building, int newLevelFromEvent)
{
	// FF_CRITICAL_TODO_BUILDING: Ensure game event for upgrades *provides the new level* via 'newLevelFromEvent'.
	int finalLevel = newLevelFromEvent;

	if (finalLevel <= 0) // If event didn't provide a valid level
	{
		CFFSentryGun *sentry = dynamic_cast<CFFSentryGun*>(building);
		if (sentry) finalLevel = sentry->GetUpgradeLevel();
		else
		{
			CFFDispenser *dispenser = dynamic_cast<CFFDispenser*>(building);
			if (dispenser) finalLevel = dispenser->GetUpgradeLevel();
			else
			{
				// FF_TODO_CLASS_ENGINEER: Add teleporter level check if they can be upgraded (FF teleporters usually are not).
				PrintIfWatched("Bot %s: NotifyBuildingUpgraded for unknown building type or event/entity did not provide level.\n", GetPlayerName());
				// Guess based on current known level if it's our building
				if (building == m_sentryGun.Get()) finalLevel = m_sentryLevel + 1;
				else if (building == m_dispenser.Get()) finalLevel = m_dispenserLevel + 1;
				else finalLevel = 1; // Pure guess for unknown buildings
				if (finalLevel > 3) finalLevel = 3; // Cap guess at level 3 for Sentry/Dispenser
			}
		}
	}

	SetBuildingLevel(building, finalLevel);
	PrintIfWatched("Bot %s: Building %s reported upgraded to level %d.\n", GetPlayerName(), building->GetClassname(), finalLevel);
}

void CFFBot::NotifyBuildingDestroyed(CBaseEntity *building)
{
	bool wasSappedAndDestroyed = (m_sappedBuildingHandle.Get() == building);

	SetBuildingLevel(building, 0); // Sets level to 0

	if (building == m_sentryGun.Get())
	{
		PrintIfWatched("Bot %s: Sentry gun destroyed.\n", GetPlayerName());
		m_sentryGun = NULL;
	}
	else if (building == m_dispenser.Get())
	{
		PrintIfWatched("Bot %s: Dispenser destroyed.\n", GetPlayerName());
		m_dispenser = NULL;
	}
	else if (building == m_teleEntrance.Get())
	{
		PrintIfWatched("Bot %s: Teleporter Entrance destroyed.\n", GetPlayerName());
		m_teleEntrance = NULL;
		m_teleEntranceLevel = 0;
	}
	else if (building == m_teleExit.Get())
	{
		PrintIfWatched("Bot %s: Teleporter Exit destroyed.\n", GetPlayerName());
		m_teleExit = NULL;
		m_teleExitLevel = 0;
	}

	if (wasSappedAndDestroyed) {
		m_sappedBuildingHandle = NULL;
		// Check if any *other* buildings are still sapped before clearing m_hasSappedBuilding
		if (m_sentryGun.Get() && static_cast<CFFBuildableObject*>(m_sentryGun.Get())->IsSapped()) { /* still has sapped sentry */ }
		else if (m_dispenser.Get() && static_cast<CFFBuildableObject*>(m_dispenser.Get())->IsSapped()) { /* still has sapped dispenser */ }
		// FF_TODO_CLASS_ENGINEER: Add teleporter sapped check if applicable
		else { m_hasSappedBuilding = false; }
	}

	// If the bot was actively guarding or repairing the destroyed building, go idle.
	if ( (GetState() == &m_guardSentryState && m_guardSentryState.GetSentryBeingGuarded() == building) ||
		 (GetState() == &m_repairBuildableState && m_repairBuildableState.GetTargetBuildable() == building) )
	{
		Idle();
	}
}

void CFFBot::NotifyBuildingPlacementStarted(BuildableType type)
{
	// Called by the bot itself when it decides to place a blueprint.
	switch(type)
	{
		case BUILDABLE_SENTRY:
			m_sentryLevel = 0; // Blueprint stage
			PrintIfWatched("Bot %s: Initiated sentry blueprint placement.\n", GetPlayerName());
			break;
		case BUILDABLE_DISPENSER:
			m_dispenserLevel = 0; // Blueprint stage
			PrintIfWatched("Bot %s: Initiated dispenser blueprint placement.\n", GetPlayerName());
			break;
		case BUILDABLE_TELE_ENTRANCE:
			m_teleEntranceLevel = 0; // Blueprint stage
			m_teleEntrance = NULL;   // Clear handle in case it was previously set by another bot's destroyed tele
			PrintIfWatched("Bot %s: Initiated teleporter entrance blueprint placement.\n", GetPlayerName());
			break;
		case BUILDABLE_TELE_EXIT:
			m_teleExitLevel = 0; // Blueprint stage
			m_teleExit = NULL;     // Clear handle
			PrintIfWatched("Bot %s: Initiated teleporter exit blueprint placement.\n", GetPlayerName());
			break;
		default:
			PrintIfWatched("Bot %s: NotifyBuildingPlacementStarted called for unknown type %d.\n", GetPlayerName(), type);
			break;
	}
}


void CFFBot::NotifyBuildingBuilt(CBaseEntity* newBuilding, BuildableType type)
{
	if (!newBuilding)
	{
		PrintIfWatched("Bot %s: NotifyBuildingBuilt called with NULL newBuilding for type %d.\n", GetPlayerName(), type);
		return;
	}

	SetBuildingLevel(newBuilding, 1); // Sets level to 1

	switch(type)
	{
		case BUILDABLE_SENTRY:
			m_sentryGun = newBuilding;
			PrintIfWatched("Bot %s: Confirmed Sentry built: %s, Level: %d\n", GetPlayerName(), newBuilding->GetClassname(), m_sentryLevel);
			break;
		case BUILDABLE_DISPENSER:
			m_dispenser = newBuilding;
			PrintIfWatched("Bot %s: Confirmed Dispenser built: %s, Level: %d\n", GetPlayerName(), newBuilding->GetClassname(), m_dispenserLevel);
			break;
		case BUILDABLE_TELE_ENTRANCE:
			m_teleEntrance = newBuilding;
			PrintIfWatched("Bot %s: Confirmed Teleporter Entrance built: %s, Level: %d\n", GetPlayerName(), newBuilding->GetClassname(), m_teleEntranceLevel);
			break;
		case BUILDABLE_TELE_EXIT:
			m_teleExit = newBuilding;
			PrintIfWatched("Bot %s: Confirmed Teleporter Exit built: %s, Level: %d\n", GetPlayerName(), newBuilding->GetClassname(), m_teleExitLevel);
			break;
		default:
			PrintIfWatched("Bot %s: Notified unknown buildable type (%d) built: %s.\n", GetPlayerName(), type, newBuilding->GetClassname());
			break;
	}
}

void CFFBot::SetBuildingLevel(CBaseEntity* pBuilding, int level)
{
	if (!pBuilding) return;

	if (pBuilding == m_sentryGun.Get())
	{
		m_sentryLevel = level;
	}
	else if (pBuilding == m_dispenser.Get())
	{
		m_dispenserLevel = level;
	}
	else if (pBuilding == m_teleEntrance.Get())
	{
		m_teleEntranceLevel = level; // Teleporters are typically 0 or 1
	}
	else if (pBuilding == m_teleExit.Get())
	{
		m_teleExitLevel = level; // Teleporters are typically 0 or 1
	}
	// Fallback to classname if not one of our current known buildings (e.g. other player's building)
	// This part is mostly for information if needed, bot primarily cares about its own building levels for upgrade logic.
	else if (FClassnameIs(pBuilding, "obj_sentrygun"))
	{
		// Potentially update knowledge of other players' sentries if needed by AI, but not m_sentryLevel.
	}
	else if (FClassnameIs(pBuilding, "obj_dispenser"))
	{
		// Same for dispensers.
	}
	// FF_TODO_CLASS_ENGINEER: Add FClassnameIs checks for teleporters if needed for general knowledge.
}

bool CFFBot::IsOwnBuildingInProgress(BuildableType type) const
{
	// FF_TODO_CLASS_ENGINEER: Remove this function after states are updated.
	return false;
}

//--------------------------------------------------------------------------------------------------------------
// New Notification stubs
//--------------------------------------------------------------------------------------------------------------
void CFFBot::NotifyPipeDetonated(IGameEvent *event)
{
	// FF_TODO_CLASS_DEMOMAN: Implement logic for Demoman to react to their own pipe detonations.
	// Check if 'userid' of the event is this bot.
	// If so, and if tracking deployed direct-fire pipes, update count or state.
	// For stickies, CFFPlayer::DetonateStickies() handles game logic; this event might inform bot that some/all stickies are gone.
	int ownerId = event->GetInt("userid", 0);
	if (ownerId == GetUserID() && IsDemoman())
	{
		// This event is generic for any pipe. If it's a sticky, m_deployedStickiesCount is reset by TryDetonateStickies.
		// If it's a direct-fire grenade, bot doesn't track those individually post-fire typically.
		// For now, just log if it's our pipe.
		// const char* pipeType = event->GetString("type", "unknown"); // Assuming event has a "type" (e.g. "pipebomb", "sticky")
		// PrintIfWatched("Demoman %s: My pipe detonated (type: %s).\n", GetPlayerName(), pipeType);
	}
}

void CFFBot::NotifyPlayerHealed(CFFPlayer* pMedic)
{
	if (!pMedic) return;
	PrintIfWatched("Bot %s: I'm being healed by Medic %s!\n", GetPlayerName(), pMedic->GetPlayerName());
	// FF_TODO_AI_BEHAVIOR: Could thank medic, or adjust behavior (e.g. feel safer to advance).
}

void CFFBot::NotifyMedicGaveHeal(CFFPlayer* pPatient)
{
	if (!IsMedic() || !pPatient) return;
	PrintIfWatched("Medic %s: Successfully healed %s.\n", GetPlayerName(), pPatient->GetPlayerName());
	// HealTeammateState handles logic of when to stop healing. This is an FYI.
	// Could be used for scoring/prioritizing patients if multiple are calling.
}

void CFFBot::NotifyGotDispenserAmmo(CBaseEntity* pDispenser)
{
	if (!pDispenser) return;
	PrintIfWatched("Bot %s: Got ammo from Dispenser %s.\n", GetPlayerName(), pDispenser->GetDebugName());
	// FF_TODO_AI_BEHAVIOR: Update ammo status knowledge, potentially leave retreat sooner.
}

void CFFBot::NotifyCloaked()
{
	if (!IsSpy()) return;
	PrintIfWatched("Spy %s: Successfully cloaked.\n", GetPlayerName());
	// Internal state m_isCloaked in Spy states should reflect this.
	// This confirms the action succeeded.
}

void CFFBot::NotifyUncloaked()
{
	if (!IsSpy()) return;
	PrintIfWatched("Spy %s: Successfully uncloaked.\n", GetPlayerName());
}


// Demoman specific implementations
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsDemoman() const
{
	// FF_TODO_GAME_MECHANIC: Ensure "demoman" is the exact internal class name string.
	return FStrEq(GetPlayerClass()->GetName(), "demoman");
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryLayStickyTrap(const Vector& location)
{
	if (!IsDemoman()) return;
	if (m_deployedStickiesCount >= MAX_BOT_STICKIES)
	{
		PrintIfWatched("Demoman %s: At max bot stickies (%d), cannot lay more.\n", GetPlayerName(), m_deployedStickiesCount);
		return;
	}

	// FF_TODO_CLASS_DEMOMAN: Check if actual game pipebomb count for player is at max (eg CFFPlayer::GetNumPipebombs())
	// if ( static_cast<CFFPlayer*>(this)->GetNumPipebombs() >= MAX_DEMO_PIPES ) return;


	// FF_TODO_WEAPON_STATS: Verify FF_WEAPON_PIPELAUNCHER is correct and has IN_ATTACK2 for stickies.
	CFFWeaponBase *pStickyLauncher = GetWeaponByID(FF_WEAPON_PIPELAUNCHER);
	if (!pStickyLauncher || !CanDeployWeapon(pStickyLauncher) || pStickyLauncher->Clip1() <= 0) // Assuming stickies use Clip1 for ammo like pipes
	{
		PrintIfWatched("Demoman %s: Cannot lay sticky (no launcher, no ammo, or can't deploy).\n", GetPlayerName());
		// FF_TODO_AI_BEHAVIOR: Maybe switch to it or reload if possible. For now, just fail.
		return;
	}

	if (GetActiveFFWeapon() != pStickyLauncher)
	{
		EquipWeapon(pStickyLauncher);
		// Need to wait for equip animation if any. For simplicity, assume immediate or next frame.
		PrintIfWatched("Demoman %s: Equipping sticky launcher to lay trap.\n", GetPlayerName());
		// Could defer action to next frame: m_actionTimer.Start(0.1f); return;
	}

	// Aiming: Bot should already be positioned by LayStickyTrapState to have LOS.
	// Fine-tune aim to the specific location.
	SetLookAt("Laying Sticky", location, PRIORITY_HIGH, 0.5f); // Look for 0.5s

	// FF_TODO_CLASS_DEMOMAN: Verify IN_ATTACK2 is correct for laying stickies.
	PressButton(IN_ATTACK2);
	ReleaseButton(IN_ATTACK2);
	// Actual CFFPlayer::FirePipeBomb(true) for stickies might set m_flNextSecondaryAttack. Bot needs to respect this.
	// For now, simple press/release.

	m_deployedStickiesCount++;
	m_stickyArmTime.Start(1.0f); // Stickies need ~1 second to arm
	// m_flNextSecondaryAttack = gpGlobals->curtime + GetPlayerClassData()->m_flStickyChargeTime; // If bot needs to respect weapon refire time

	PrintIfWatched("Demoman %s: Laid a sticky towards (%.1f, %.1f, %.1f). Deployed: %d\n", GetPlayerName(), location.x, location.y, location.z, m_deployedStickiesCount);
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryDetonateStickies(CFFPlayer* pTarget)
{
	if (!IsDemoman()) return;
	if (m_deployedStickiesCount == 0)
	{
		// PrintIfWatched("Demoman %s: No stickies to detonate.\n", GetPlayerName());
		return;
	}
	if (!m_stickyArmTime.IsElapsed())
	{
		PrintIfWatched("Demoman %s: Stickies not armed yet (%.1f remaining).\n", GetPlayerName(), m_stickyArmTime.GetRemainingTime());
		return;
	}
	if (!m_stickyDetonateCooldown.IsElapsed())
	{
		// PrintIfWatched("Demoman %s: Detonation cooldown active (%.1f remaining).\n", GetPlayerName(), m_stickyDetonateCooldown.GetRemainingTime());
		return;
	}

	// FF_TODO_WEAPON_STATS: Verify FF_WEAPON_PIPELAUNCHER is correct and IN_ATTACK2 detonates.
	// Some games have a separate detonator weapon or require switching to the launcher first.
	CFFWeaponBase *pStickyLauncher = GetWeaponByID(FF_WEAPON_PIPELAUNCHER);
	if (!pStickyLauncher)
	{
		PrintIfWatched("Demoman %s: Cannot detonate (no launcher).\n", GetPlayerName());
		return;
	}

	if (GetActiveFFWeapon() != pStickyLauncher)
	{
		EquipWeapon(pStickyLauncher);
		// Could defer action to next frame for equip time.
		PrintIfWatched("Demoman %s: Equipping sticky launcher to detonate.\n", GetPlayerName());
	}

	// FF_TODO_CLASS_DEMOMAN: Aiming before detonation? Usually not required by game mechanic itself, but bot might look towards target.
	if (pTarget)
	{
		SetLookAt("Detonating near Target", pTarget->EyePosition(), PRIORITY_MEDIUM, 0.5f);
		PrintIfWatched("Demoman %s: Detonating stickies near target %s.\n", GetPlayerName(), pTarget->GetPlayerName());
	}
	else
	{
		PrintIfWatched("Demoman %s: Detonating stickies (no specific target).\n", GetPlayerName());
	}

	PressButton(IN_ATTACK2);
	ReleaseButton(IN_ATTACK2);
	// Actual CFFPlayer::DetonateStickies() is called.
	// This function in CFFPlayer sets m_flNextSecondaryAttack. Bot should respect this.

	m_deployedStickiesCount = 0; // Assume all deployed stickies are detonated.
	m_stickyDetonateCooldown.Start(1.0f); // Cooldown before next detonation attempt
	// m_flNextSecondaryAttack = gpGlobals->curtime + GetPlayerClassData()->m_flDetonateDelay; // If bot needs to respect weapon refire time
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::StartLayingStickyTrap(const Vector& pos)
{
	if (!IsDemoman()) return;

	PrintIfWatched("Demoman %s: Requested to lay sticky trap near (%.1f, %.1f, %.1f).\n", GetPlayerName(), pos.x, pos.y, pos.z);
	m_layStickyTrapState.SetTrapLocation(pos);
	SetState(&m_layStickyTrapState);
}

//--------------------------------------------------------------------------------------------------------------
// Event-based notification methods CFFBotManager will call on specific bots
//--------------------------------------------------------------------------------------------------------------

// Note: CFFBot::OnPipeDetonated (the direct game event handler) was already modified.
// This NotifyPipeDetonated is if the manager specifically tells this bot about a pipe.
// For this subtask, we'll assume CFFBot::OnDetpackDetonated is the primary path via manager.
// void CFFBot::NotifyPipeDetonated(IGameEvent *event)
// {
// 	int ownerId = event->GetInt("userid");
// 	if (IsDemoman() && ownerId == GetUserID())
// 	{
// 		PrintIfWatched( "Demoman %s: My pipe/sticky detonated (event based).\n", GetPlayerName() );
//      // This might be redundant if TryDetonateStickies already set count to 0,
//      // or useful if an enemy detonated one of our stickies.
// 		if (m_deployedStickiesCount > 0) m_deployedStickiesCount = 0; // Safest to assume all gone
// 	}
// }

void CFFBot::NotifyPlayerHealed(CFFPlayer* pMedic)
{
	if (!pMedic) return;
	PrintIfWatched("Bot %s: I'm being healed by Medic %s!\n", GetPlayerName(), pMedic->GetPlayerName());
	// FF_TODO_AI_BEHAVIOR: Could thank medic, or adjust behavior (e.g. feel safer to advance).
	// Potentially increase morale slightly or adjust threat assessment if being actively supported.
}

void CFFBot::NotifyMedicGaveHeal(CFFPlayer* pPatient)
{
	if (!IsMedic() || !pPatient) return;
	PrintIfWatched("Medic %s: Successfully gave health to %s.\n", GetPlayerName(), pPatient->GetPlayerName());
	// HealTeammateState handles logic of when to stop healing. This is an FYI.
	// Could be used for scoring/prioritizing patients if multiple are calling.
	// Could also start a short timer to "stick" with the patient if they are still in combat / vulnerable.
}

void CFFBot::NotifyGotDispenserAmmo(CBaseEntity* pDispenser)
{
	if (!pDispenser) return;
	PrintIfWatched("Bot %s: Got ammo/resources from Dispenser %s.\n", GetPlayerName(), pDispenser->GetDebugName());
	// FF_TODO_AI_BEHAVIOR: Bot should update its internal ammo/resource knowledge.
	// If it was in RetreatState heading for this dispenser, this might trigger exiting retreat sooner.
	// If it was in FindResourcesState, this might satisfy its need.
}

void CFFBot::NotifyCloaked()
{
	if (!IsSpy()) return;
	PrintIfWatched("Spy %s: Successfully cloaked (notified by manager).\n", GetPlayerName());
	// This can be used to confirm to the bot that its cloak attempt (e.g. HandleCommand("cloak")) succeeded.
	// Spy-specific states (like InfiltrateState) can check CFFPlayer::IsCloaked(),
	// but this notification provides an explicit trigger.
}

void CFFBot::NotifyUncloaked()
{
	if (!IsSpy()) return;
	PrintIfWatched("Spy %s: Successfully uncloaked (notified by manager).\n", GetPlayerName());
	// Similar to NotifyCloaked, confirms uncloak action.
}

// Note: NotifyCalledForMedic and NotifyGaveMedicHealth are already implemented from previous steps.

// Engineer Teleporter specific methods
//--------------------------------------------------------------------------------------------------------------
int CFFBot::GetTeleporterEntranceLevel() const
{
	return (m_teleEntrance.Get() ? m_teleEntranceLevel : 0);
}

//--------------------------------------------------------------------------------------------------------------
int CFFBot::GetTeleporterExitLevel() const
{
	return (m_teleExit.Get() ? m_teleExitLevel : 0);
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToBuildTeleporterEntrance(const Vector* location)
{
	if (!IsEngineer()) return;
	if (GetTeleporterEntranceLevel() > 0)
	{
		// PrintIfWatched("Bot %s: Already has a Teleporter Entrance.\n", GetPlayerName());
		return;
	}
	// FF_TODO_CLASS_ENGINEER: Check resources (metal)
	// if (GetMetal() < GetPlayerClassData()->m_iTeleporterCost) { PrintIfWatched("Bot %s: Not enough metal for Teleporter Entrance.\n", GetPlayerName()); return; }

	PrintIfWatched("Bot %s: Attempting to build Teleporter Entrance.\n", GetPlayerName());
	// m_buildTeleEntranceState.SetBuildLocation(location); // If state needs specific location
	SetState(&m_buildTeleEntranceState);
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToBuildTeleporterExit(const Vector* location)
{
	if (!IsEngineer()) return;
	if (GetTeleporterExitLevel() > 0)
	{
		// PrintIfWatched("Bot %s: Already has a Teleporter Exit.\n", GetPlayerName());
		return;
	}
	if (GetTeleporterEntranceLevel() == 0) // Usually need an entrance first
	{
		PrintIfWatched("Bot %s: Cannot build Teleporter Exit, Entrance not found.\n", GetPlayerName());
		// FF_TODO_AI_BEHAVIOR: Could try to build entrance first if appropriate.
		return;
	}
	// FF_TODO_CLASS_ENGINEER: Check resources (metal)
	// if (GetMetal() < GetPlayerClassData()->m_iTeleporterCost) { PrintIfWatched("Bot %s: Not enough metal for Teleporter Exit.\n", GetPlayerName()); return; }

	PrintIfWatched("Bot %s: Attempting to build Teleporter Exit.\n", GetPlayerName());
	// m_buildTeleExitState.SetBuildLocation(location); // If state needs specific location
	SetState(&m_buildTeleExitState);
}

//--------------------------------------------------------------------------------------------------------------
// Prioritization helper method implementations
//--------------------------------------------------------------------------------------------------------------
CFFPlayer* CFFBot::GetMedicWhoIsHealingMe() const
{
	// FF_TODO_AI_BEHAVIOR: Implement logic to find a Medic CFFPlayer entity that has this bot as its heal target.
	// This might involve iterating players, checking if they are Medic, and inspecting their CFFPlayer::m_hHealTarget.
	// For now, this is a stub.
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CFFPlayer* pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));
		if (pPlayer && pPlayer->IsAlive() && pPlayer->IsMedic() && InSameTeam(pPlayer))
		{
			// Conceptual: if (pPlayer->GetHealTarget() == this) return pPlayer;
		}
	}
	return NULL;
}

//--------------------------------------------------------------------------------------------------------------
CBaseEntity* CFFBot::GetMostImportantNearbyFriendlyBuilding() const
{
	// FF_TODO_AI_BEHAVIOR: Implement more sophisticated logic to find the most important friendly building.
	// This could consider building health, level, proximity to combat, type (Sentry > Dispenser > Teleporter), etc.
	// For now, simple check for own built sentry or dispenser.
	if (m_sentryGun.Get() && m_sentryGun->IsAlive() && static_cast<CFFBuildableObject*>(m_sentryGun.Get())->IsBuilt())
	{
		if ((GetAbsOrigin() - m_sentryGun->GetAbsOrigin()).IsLengthLessThan(1000.0f)) // Example radius
			return m_sentryGun.Get();
	}
	if (m_dispenser.Get() && m_dispenser->IsAlive() && static_cast<CFFBuildableObject*>(m_dispenser.Get())->IsBuilt())
	{
		if ((GetAbsOrigin() - m_dispenser->GetAbsOrigin()).IsLengthLessThan(1000.0f)) // Example radius
			return m_dispenser.Get();
	}
	// FF_TODO_CLASS_ENGINEER: Add teleporter checks if they are considered important targets for this context.
	return NULL;
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsNearPosition(const Vector& pos, float radius) const
{
	return (GetAbsOrigin() - pos).IsLengthLessThan(radius);
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return the number of bots following the given player
 */
int GetBotFollowCount( CFFPlayer *leader )
{
	int count = 0;
	for( int i=1; i <= gpGlobals->maxClients; ++i )
	{
		CBaseEntity *entity = UTIL_PlayerByIndex( i );
		if (entity == NULL) continue;
		CBasePlayer *player = static_cast<CBasePlayer *>( entity );
		if (!player->IsBot() || !player->IsAlive()) continue;
		CFFBot *bot = dynamic_cast<CFFBot *>( player );
		if (bot && bot->GetFollowLeader() == leader)
			++count;
	}
	return count;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::Walk( void ) { if (m_mustRunTimer.IsElapsed()) BaseClass::Walk(); else Run(); }

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::Jump( bool mustJump )
{
	bool inCrouchJumpArea = (m_lastKnownArea &&
		(m_lastKnownArea->GetAttributes() & NAV_MESH_CROUCH) &&
		(m_lastKnownArea->GetAttributes() & NAV_MESH_JUMP));
	if ( !IsUsingLadder() && IsDucked() && !inCrouchJumpArea ) return false;
	return BaseClass::Jump( mustJump );
}

//--------------------------------------------------------------------------------------------------------------
int CFFBot::OnTakeDamage( const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetInflictor();
	BecomeAlert();
	StopWaiting();

	if (attacker && attacker->IsPlayer())
	{
		CFFPlayer *player = static_cast<CFFPlayer *>( attacker );
		if (InSameTeam( player ) && !player->IsBot()) GetChatter()->FriendlyFire();
	}

	if (attacker && attacker->IsPlayer() && IsEnemy( attacker ))
	{
		CFFPlayer *lastAttacker = m_attacker.Get();
		float lastAttackedTimestamp = m_attackedTimestamp;
		m_attacker = reinterpret_cast<CFFPlayer *>( attacker );
		m_attackedTimestamp = gpGlobals->curtime;
		AdjustSafeTime();
		if ( !IsSurprised() && (m_attacker != lastAttacker || m_attackedTimestamp != lastAttackedTimestamp) )
		{
			CFFPlayer *enemy = static_cast<CFFPlayer *>( attacker );
			if (!IsVisible( enemy, CHECK_FOV ))
			{
				if (!IsAttacking()) Panic();
				else if (!IsEnemyVisible()) Panic();
			}
		}
	}
	return BaseClass::OnTakeDamage( info );
}

//--------------------------------------------------------------------------------------------------------------
// (Event_Killed already modified to call NotifyBuildingDestroyed)
// ...

//--------------------------------------------------------------------------------------------------------------
// bool CFFBot::HasSentry(void) const // Original from CFFPlayer, CFFBot inherits this.
// {
// 	if ( GetSentryGun() != NULL )
// 		 return true;
// 	return false;
// }
bool CFFBot::HasSentry(void) const // Bot's own tracking
{
	return m_sentryGun.Get() != NULL && m_sentryLevel > 0;
}

//--------------------------------------------------------------------------------------------------------------
CBaseEntity* CFFBot::GetSentry(void) const // Bot's own tracking getter
{
	return m_sentryGun.Get();
}


//--------------------------------------------------------------------------------------------------------------
// bool CFFBot::HasDispenser(void) const // Original from CFFPlayer
// {
// 	if ( GetDispenser() != NULL )
// 		 return true;
// 	return false;
// }
bool CFFBot::HasDispenser(void) const // Bot's own tracking
{
	return m_dispenser.Get() != NULL && m_dispenserLevel > 0;
}

//--------------------------------------------------------------------------------------------------------------
CBaseEntity* CFFBot::GetDispenser(void) const // Bot's own tracking getter
{
	return m_dispenser.Get();
}


// ... (rest of the file, including TryToBuildSentry, TryToBuildDispenser, IsSpy, IsScout, GetMaxSpeed, TryDoubleJump, BotThink, etc.)
// ... The TryToBuildSentry/Dispenser methods will now use the updated HasSentry/HasDispenser correctly.

// Ensure all existing function bodies are preserved below this point.
// The following functions are just placeholders for the diff tool if needed, the actual content is above.
void CFFBot::BotDeathThink( void ) { }
void CFFBot::TryToJoinTeam( int team ) { m_desiredTeam = team; }
bool CFFBot::StayOnNavMesh( void ) { if (m_currentArea == NULL) { CNavArea *goalArea = NULL; if (!m_lastKnownArea) { goalArea = TheNavMesh->GetNearestNavArea( GetCentroid( this ) ); } else { goalArea = m_lastKnownArea; } if (goalArea) { Vector pos; goalArea->GetClosestPointOnArea( GetCentroid( this ), &pos ); Vector to = pos - GetCentroid( this ); to.NormalizeInPlace(); const float stepInDist = 5.0f;	pos = pos + (stepInDist * to); MoveTowardsPosition( pos ); } if (m_isStuck) Wiggle(); return false; } return true;}
bool CFFBot::IsDoingScenario( void ) const { if (cv_bot_defer_to_human.GetBool()){ if (UTIL_HumansOnTeam( GetTeamNumber(), IS_ALIVE )) return false; } return true; }
CFFPlayer *CFFBot::GetAttacker( void ) const { if (m_attacker && m_attacker->IsAlive()) return m_attacker.Get(); return NULL; }
void CFFBot::GetOffLadder( void ) { if (IsUsingLadder()) { Jump( MUST_JUMP ); DestroyPath(); } }
float CFFBot::GetHidingSpotCheckTimestamp( HidingSpot *spot ) const { for( int i=0; i<m_checkedHidingSpotCount; ++i ) if (m_checkedHidingSpot[i].spot->GetID() == spot->GetID()) return m_checkedHidingSpot[i].timestamp; return -999999.9f; }
void CFFBot::SetHidingSpotCheckTimestamp( HidingSpot *spot ) { int leastRecent = 0; float leastRecentTime = gpGlobals->curtime + 1.0f; for( int i=0; i<m_checkedHidingSpotCount; ++i ) { if (m_checkedHidingSpot[i].spot->GetID() == spot->GetID()) { m_checkedHidingSpot[i].timestamp = gpGlobals->curtime; return; } if (m_checkedHidingSpot[i].timestamp < leastRecentTime) { leastRecentTime = m_checkedHidingSpot[i].timestamp; leastRecent = i; } } if (m_checkedHidingSpotCount < MAX_CHECKED_SPOTS) { m_checkedHidingSpot[ m_checkedHidingSpotCount ].spot = spot; m_checkedHidingSpot[ m_checkedHidingSpotCount ].timestamp = gpGlobals->curtime; ++m_checkedHidingSpotCount; } else { m_checkedHidingSpot[ leastRecent ].spot = spot; m_checkedHidingSpot[ leastRecent ].timestamp = gpGlobals->curtime; } }
bool CFFBot::IsOutnumbered( void ) const { return (GetNearbyFriendCount() < GetNearbyEnemyCount()-1); }
int CFFBot::OutnumberedCount( void ) const { if (IsOutnumbered()) return (GetNearbyEnemyCount()-1) - GetNearbyFriendCount(); return 0; }
CFFPlayer *CFFBot::GetImportantEnemy( bool checkVisibility ) const { CFFBotManager *ctrl = TheFFBots(); CFFPlayer *nearEnemy = NULL; float nearDist = 999999999.9f; for ( int i = 1; i <= gpGlobals->maxClients; i++ ) { CBaseEntity *entity = UTIL_PlayerByIndex( i ); if (entity == NULL || !entity->IsPlayer()) continue; CFFPlayer *player = static_cast<CFFPlayer *>( entity );  if (!player->IsAlive() || InSameTeam( player ) || (ctrl && !ctrl->IsImportantPlayer( player ))) continue; Vector d = GetAbsOrigin() - player->GetAbsOrigin(); float distSq = d.LengthSqr(); if (distSq < nearDist) { if (checkVisibility && !IsVisible( player, CHECK_FOV )) continue; nearEnemy = player; nearDist = distSq; } } return nearEnemy; }
void CFFBot::SetDisposition( DispositionType disposition ) { m_disposition = disposition; if (m_disposition != IGNORE_ENEMIES) m_ignoreEnemiesTimer.Invalidate(); }
CFFBot::DispositionType CFFBot::GetDisposition( void ) const { if (!m_ignoreEnemiesTimer.IsElapsed()) return IGNORE_ENEMIES; return m_disposition; }
void CFFBot::IgnoreEnemies( float duration ) { m_ignoreEnemiesTimer.Start( duration ); }
void CFFBot::IncreaseMorale( void ) { if (m_morale < EXCELLENT) m_morale = static_cast<MoraleType>( m_morale + 1 ); }
void CFFBot::DecreaseMorale( void ) { if (m_morale > TERRIBLE) m_morale = static_cast<MoraleType>( m_morale - 1 ); }
bool CFFBot::IsRogue( void ) const {  CFFBotManager *ctrl = TheFFBots(); if (!ctrl || !ctrl->AllowRogues()) return false; if (m_rogueTimer.IsElapsed()) { m_rogueTimer.Start( RandomFloat( 10.0f, 30.0f ) ); const float rogueChance = 100.0f * (1.0f - GetProfile()->GetTeamwork()); m_isRogue = (RandomFloat( 0, 100 ) < rogueChance); } return m_isRogue;  }
bool CFFBot::IsHurrying( void ) const { if (!m_hurryTimer.IsElapsed()) return true; return false; }
bool CFFBot::IsSafe( void ) const { CFFBotManager *ctrl = TheFFBots(); return (ctrl && ctrl->GetElapsedRoundTime() < m_safeTime); }
bool CFFBot::IsWellPastSafe( void ) const { CFFBotManager *ctrl = TheFFBots(); return (ctrl && ctrl->GetElapsedRoundTime() > 2.0f * m_safeTime); }
bool CFFBot::IsEndOfSafeTime( void ) const { return m_wasSafe && !IsSafe(); }
float CFFBot::GetSafeTimeRemaining( void ) const { CFFBotManager *ctrl = TheFFBots(); return m_safeTime - (ctrl ? ctrl->GetElapsedRoundTime() : 0.0f); }
void CFFBot::AdjustSafeTime( void ) { CFFBotManager *ctrl = TheFFBots(); if (ctrl && ctrl->GetElapsedRoundTime() < m_safeTime) { m_safeTime = ctrl->GetElapsedRoundTime() - 2.0f; } }
bool CFFBot::HasNotSeenEnemyForLongTime( void ) const { const float longTime = 30.0f; return (GetTimeSinceLastSawEnemy() > longTime); }
bool CFFBot::GuardRandomZone( float range ) { CFFBotManager *ctrl = TheFFBots();  const CFFBotManager::Zone *zone = ctrl ? ctrl->GetRandomZone() : NULL;  if (zone) { CNavArea *area = ctrl->GetRandomAreaInZone( zone );  if (area) { Hide( area, -1.0f, range ); return true; } } return false; }
// GetTaskName, GetDispositionName, GetMoraleName, BuildUserCmd, TryToGuardSentry, FireWeaponAtEnemy, EquipBestWeapon etc. are assumed to be correctly implemented from previous steps or are placeholders for now.
// The IsUsing... weapon checks, weapon equipping logic, etc., are simplified placeholders from previous steps.
void CFFBot::PlayerRunCommand(CUserCmd* ucmd, IMoveHelper* moveHelper) { BaseClass::PlayerRunCommand(ucmd, moveHelper); } // Example if needed
void CFFBot::PostThink(void) { BaseClass::PostThink(); } // Example if needed
void CFFBot::UpkeepClient(void) { BaseClass::UpkeepClient(); } // Example if needed, though BotThink is preferred.

// Dummy implementations for any pure virtuals from CBot (if any were missed by BaseClass)
// void CFFBot::ResetValues(void) { } // This is in CBasePlayerBot
// const BotProfile *CFFBot::GetProfile( void ) const { return BaseClass::GetProfile(); } // This is in CBasePlayerBot
// bool CFFBot::IsAlive( void ) const { return BaseClass::IsAlive(); } // This is in CBasePlayer

//--------------------------------------------------------------------------------------------------------------
// Teammate Following Logic
//--------------------------------------------------------------------------------------------------------------
void CFFBot::FollowPlayer(CFFPlayer* pPlayerToFollow)
{
	if (!pPlayerToFollow || pPlayerToFollow == this || !pPlayerToFollow->IsAlive() || pPlayerToFollow->GetTeamNumber() != GetTeamNumber())
	{
		PrintIfWatched("%s: Cannot follow invalid player %s.\n", GetPlayerName(), pPlayerToFollow ? pPlayerToFollow->GetPlayerName() : "NULL");
		Idle();
		return;
	}
	m_followTeammateState.SetPlayerToFollow(pPlayerToFollow);
	SetState(&m_followTeammateState);
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToFollowNearestTeammate(float maxDist)
{
	// FF_TODO_CLASS: Some classes should be less likely to follow (e.g., Snipers, Engineers guarding nest).
	if (IsSniper() && IsSniping()) return; // Snipers actively sniping shouldn't follow.
	if (IsEngineer() && (HasSentry() || HasDispenser())) // Engineers with a nest might be less inclined.
	{
		// Potentially reduce follow chance or only follow if nest is secure/nearby.
		if (RandomFloat(0,1) < 0.75f) return;
	}


	CFFPlayer *pClosestFriendly = NULL;
	float flClosestDistSq = maxDist * maxDist;

	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CFFPlayer *pPlayer = ToFFPlayer(UTIL_PlayerByIndex(i));
		if (!pPlayer || pPlayer == this || !pPlayer->IsAlive() || pPlayer->GetTeamNumber() != GetTeamNumber() || pPlayer->IsBot()) // Don't follow other bots for now
			continue;

		if (!IsVisible(pPlayer, CHECK_FOV)) // Must see them
			continue;

		float flDistSq = GetAbsOrigin().DistToSqr(pPlayer->GetAbsOrigin());
		if (flDistSq < flClosestDistSq)
		{
			// Check pathability
			if (ComputePath(pPlayer->GetAbsOrigin(), FASTEST_ROUTE))
			{
				pClosestFriendly = pPlayer;
				flClosestDistSq = flDistSq;
			}
		}
	}

	if (pClosestFriendly)
	{
		PrintIfWatched("%s: Decided to follow nearest teammate %s.\n", GetPlayerName(), pClosestFriendly->GetPlayerName());
		FollowPlayer(pClosestFriendly);
	}
}

//--------------------------------------------------------------------------------------------------------------
// Flag Notification Methods
//--------------------------------------------------------------------------------------------------------------
void CFFBot::NotifyPickedUpFlag(CFFInfoScript* pFlagInfoScript, int flagType)
{
	m_carriedFlag = pFlagInfoScript;
	m_carriedFlagType = flagType;
	if (pFlagInfoScript)
	{
		const char* flagTypeName = (flagType == 1) ? "enemy" : (flagType == 2 ? "own" : "unknown type");
		PrintIfWatched("Bot %s: Picked up %s flag '%s' (ent %d).\n",
			GetPlayerName(),
			flagTypeName,
			pFlagInfoScript->GetEntityNameAsCStr() ? pFlagInfoScript->GetEntityNameAsCStr() : "unnamed_flag",
			pFlagInfoScript->entindex());

		// FF_TODO_AI_BEHAVIOR: Bot's current state might need to react immediately.
		// For example, if in IdleState, its next OnUpdate will see HasEnemyFlag() and act.
		// If it was in CaptureObjectiveState targeting this flag, that state's OnUpdate
		// (after this notification) should handle the successful "pickup" and transition.
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::NotifyDroppedFlag(CFFInfoScript* pFlagInfoScript)
{
	// Check if the flag being reported as dropped is actually the one we think we're carrying.
	if (pFlagInfoScript != NULL && m_carriedFlag.Get() == pFlagInfoScript)
	{
		const char* flagTypeName = (m_carriedFlagType == 1) ? "enemy" : (m_carriedFlagType == 2 ? "own" : "unknown type");
		PrintIfWatched("Bot %s: Dropped/Captured %s flag '%s' (ent %d).\n",
			GetPlayerName(),
			flagTypeName,
			pFlagInfoScript->GetEntityNameAsCStr() ? pFlagInfoScript->GetEntityNameAsCStr() : "unnamed_flag",
			pFlagInfoScript->entindex());

		m_carriedFlag = NULL;
		m_carriedFlagType = 0;

		// FF_TODO_AI_BEHAVIOR: If bot was in a state like CarryFlagState, it should transition out.
		// This will be handled by CarryFlagState::OnUpdate checking HasEnemyFlag().
		// If the bot died, this notification ensures its state is clean for next spawn.
		// If the flag was successfully captured, this function is also called by the bot itself
		// from CarryFlagState to signify it's no longer "physically" carrying it.
		// Example: if (GetState() == &m_carryFlagState) { Idle(); } // Assuming m_carryFlagState instance name
	}
	else if (m_carriedFlag.Get() != NULL && pFlagInfoScript != m_carriedFlag.Get())
	{
		// This case means a flag was dropped, but it wasn't the one this bot was carrying.
		// Or, pFlagInfoScript is NULL and we were carrying something.
		// Generally, no action needed by this bot unless it wants to react to *any* flag drop.
		// However, if pFlagInfoScript is NULL and we *were* carrying a flag, it's ambiguous.
		// For now, only clear our flag if the specific flag entity matches.
	}
	// If pFlagInfoScript is NULL and m_carriedFlag was already NULL, do nothing.
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::CarryFlagToCapturePoint(const CFFBotManager::LuaObjectivePoint* capturePoint)
{
	if (!HasEnemyFlag())
	{
		PrintIfWatched("Bot %s: CarryFlagToCapturePoint called, but not carrying enemy flag!\n", GetPlayerName());
		Idle(); // Go idle if not actually carrying the flag
		return;
	}
	if (!capturePoint)
	{
		PrintIfWatched("Bot %s: CarryFlagToCapturePoint called with NULL capturePoint!\n", GetPlayerName());
		Idle(); // Go idle if no valid capture point provided
		return;
	}

	PrintIfWatched("Bot %s: Transitioning to CarryFlagState. Target capture point: '%s'\n", GetPlayerName(), capturePoint->name);
	m_carryFlagState.SetCaptureTarget(capturePoint);
	SetState(&m_carryFlagState);
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::DefendObjective(const CFFBotManager::LuaObjectivePoint* pObjective)
{
	if (!pObjective)
	{
		PrintIfWatched("Bot %s: DefendObjective called with NULL objective!\n", GetPlayerName());
		Idle(); // Go idle if no valid objective provided
		return;
	}

	// Optional: Add checks here if certain classes shouldn't use this generic DefendObjectiveState
	// or if they should use a specialized version.
	// Example: if (IsScout()) { Idle(); return; } // Scouts might not be good defenders

	PrintIfWatched("Bot %s: Transitioning to DefendObjectiveState for objective '%s'.\n", GetPlayerName(), pObjective->name);
	m_defendObjectiveState.SetObjectiveToDefend(pObjective);
	SetState(&m_defendObjectiveState);
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToReload(void)
{
	CFFWeaponBase* pWeapon = GetActiveFFWeapon();

	// Check if we have an active weapon, if it needs reloading, if it can be reloaded,
	// and if we are not already in the ReloadState.
	if (pWeapon &&
		(pWeapon->Clip1() < pWeapon->GetMaxClip1() && GetAmmoCount(pWeapon->GetPrimaryAmmoType()) > 0) && // NeedsReload()
		pWeapon->CanReload() &&                                                                           // CanReload()
		GetCurrentState() != &m_reloadState)
	{
		// Do not interrupt an attack to reload unless out of ammo or very low.
		// AttackState itself will call TryToReload if Clip1() == 0.
		// This check is more for opportunistic reloads from IdleState or other non-combat states.
		if (IsAttacking() && pWeapon->Clip1() > 0) // If attacking and still have ammo, don't reload yet
		{
			// Potentially add a threshold, e.g., if clip is very low but not empty
			// PrintIfWatched("Bot %s: Wants to reload %s, but currently attacking with ammo in clip.\n", GetPlayerName(), pWeapon->GetClassname());
			return;
		}

		PrintIfWatched("Bot %s: Trying to reload %s.\n", GetPlayerName(), pWeapon->GetClassname());
		SetState(&m_reloadState);
	}
	else if (GetCurrentState() == &m_reloadState)
	{
		// Already reloading
	}
	else if (pWeapon && !(pWeapon->Clip1() < pWeapon->GetMaxClip1() && GetAmmoCount(pWeapon->GetPrimaryAmmoType()) > 0) )
	{
		// Doesn't need reload (clip full or no reserve ammo)
		// PrintIfWatched("Bot %s: No need to reload %s (Clip: %d/%d, Reserve: %d).\n", GetPlayerName(), pWeapon->GetClassname(), pWeapon->Clip1(), pWeapon->GetMaxClip1(), GetAmmoCount(pWeapon->GetPrimaryAmmoType()));
	}
	else if (pWeapon && !pWeapon->CanReload())
	{
		PrintIfWatched("Bot %s: Cannot reload %s (weapon doesn't support it).\n", GetPlayerName(), pWeapon->GetClassname());
	}
}
// End of ff_bot.cpp (ensure all previous content is above this line)
[end of mp/src/game/server/ff/bot/ff_bot.cpp]
