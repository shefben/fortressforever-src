//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../../../shared/ff/ff_gamerules.h"
#include "ff_bot_state_buy.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../../../shared/ff/weapons/ff_weapon_parse.h"
#include "../ff_gamestate.h"
#include "../ff_bot_weapon_id.h" // For WeaponIDToAliasFF, GetWeaponClassTypeFF, WeaponClassTypeFF enum

// Local bot utility headers
#include "../bot_constants.h"  // For BotDifficultyType, TEAM_TERRORIST, TEAM_CT, WEAPON_SLOT_PISTOL, FFWeaponID etc.
#include "../bot_profile.h"    // For BotProfile, GetProfile()
#include "../bot_util.h"       // For PrintIfWatched


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
ConVar bot_loadout( "bot_loadout", "", FCVAR_CHEAT, "bots are given these items at round start" );
ConVar bot_randombuy( "bot_randombuy", "0", FCVAR_CHEAT, "should bots ignore their prefered weapons and just buy weapons at random?" );

//--------------------------------------------------------------------------------------------------------------
// TODO_FF: This function needs to be updated to use FF weapon IDs and team definitions correctly.
// It should likely use GetWeaponClassTypeFF or similar from ff_bot_weapon_id.h/cpp.
static bool HasDefaultPistol( CFFBot *me )
{
	if (!me) return false;
	CFFWeaponBase *pistol = (CFFWeaponBase *)me->Weapon_GetSlot( WEAPON_SLOT_PISTOL ); // WEAPON_SLOT_PISTOL from bot_constants.h

	if (pistol == NULL)
		return false;

	// This logic is highly dependent on how default pistols are defined in FF and if they have unique FFWeaponIDs.
	// Example placeholder:
	// FFWeaponID pistolId = pistol->GetWeaponID();
	// if (me->GetTeamNumber() == TEAM_RED_FF && pistolId == FF_WEAPON_DEFAULT_RED_PISTOL) return true;
	// if (me->GetTeamNumber() == TEAM_BLUE_FF && pistolId == FF_WEAPON_DEFAULT_BLUE_PISTOL) return true;

	// Placeholder logic from CS, likely incorrect for FF:
	// const CFFWeaponInfo *glockInfo = BotProfileManager::GetWeaponInfo( WEAPON_GLOCK ); // WEAPON_GLOCK is CS specific, BotProfileManager needs FF context
	// const CFFWeaponInfo *uspInfo = BotProfileManager::GetWeaponInfo( WEAPON_USP );       // WEAPON_USP is CS specific
	// if (glockInfo && me->GetTeamNumber() == TEAM_TERRORIST && pistol->GetWeaponID() == glockInfo->m_weaponId ) return true;
	// if (uspInfo && me->GetTeamNumber() == TEAM_CT && pistol->GetWeaponID() == uspInfo->m_weaponId ) return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Buy weapons, armor, etc.
 */
void BuyState::OnEnter( CFFBot *me )
{
	if (!me || !TheFFBots() || !me->GetProfile()) return; // Null checks

	m_retries = 0;
	m_prefRetries = 0;
	m_prefIndex = 0;

	const char *cheatWeaponString = bot_loadout.GetString();
	if ( cheatWeaponString && *cheatWeaponString )
	{
		m_doneBuying = false;
	}
	else
	{
		if (me->m_iAccount < cv_bot_eco_limit.GetFloat())
		{
			PrintIfWatched(me, "Saving money for next round.\n" ); // Updated PrintIfWatched
			m_doneBuying = true;
		}
		else
		{
			m_doneBuying = false;
		}
	}

	m_isInitialDelay = true;
	me->EquipBestWeapon( true ); // Was MUST_EQUIP

	m_buyDefuseKit = false;
	m_buyShield = false;

	// TODO_FF: CS Specific team and scenario logic (defuse kit, shield)
	// if (me->GetTeamNumber() == TEAM_CT)
	// {
	//	if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_DEFUSE_BOMB)
	//	{
	//		if (FFGameRules() && FFGameRules()->IsCareer() == false)
	//		{
	//			if (RandomFloat( 0.0f, 100.0f ) < (100.0f * (me->GetProfile()->GetSkill() + 0.2f)))
	//				m_buyDefuseKit = true;
	//		}
	//	}
	//	if (!me->HasPrimaryWeapon() && TheFFBots()->AllowTacticalShield()) { /* ... shield buy logic ... */ }
	// }

	if (TheFFBots()->AllowGrenades()) m_buyGrenade = (RandomFloat( 0.0f, 100.0f ) < 33.3f);
	else m_buyGrenade = false;

	m_buyPistol = false;
	if (TheFFBots()->AllowPistols())
	{
		if (me->Weapon_GetSlot( WEAPON_SLOT_PISTOL ))
		{
			if (HasDefaultPistol( me ))
			{
				if (!TheFFBots()->AllowShotguns() && !TheFFBots()->AllowSubMachineGuns() &&
					!TheFFBots()->AllowRifles() && !TheFFBots()->AllowMachineGuns() &&
					/*!TheFFBots()->AllowTacticalShield() &&*/ !TheFFBots()->AllowSnipers()) // Shield check commented
				{
					m_buyPistol = (RandomFloat( 0, 100 ) < 75.0f);
				}
				else if (me->m_iAccount < 1000) m_buyPistol = (RandomFloat( 0, 100 ) < 75.0f);
				else m_buyPistol = (RandomFloat( 0, 100 ) < 33.3f);
			}
		}
		else m_buyPistol = true;
	}
}

// TODO_FF: This enum and the BuyInfo struct and tables are CS-specific and should be replaced
// by using g_FFBotWeaponTranslations and WeaponClassTypeFF from ff_bot_weapon_id.h/cpp
// For now, leaving them to show where FF adaptation is needed.
/*
enum WeaponType { PISTOL, SHOTGUN, SUB_MACHINE_GUN, RIFLE, MACHINE_GUN, SNIPER_RIFLE, GRENADE, NUM_WEAPON_TYPES };
struct BuyInfo { WeaponType type; bool preferred; const char *buyAlias; };
#define PRIMARY_WEAPON_BUY_COUNT 13
#define SECONDARY_WEAPON_BUY_COUNT 3
static BuyInfo primaryWeaponBuyInfoCT[ PRIMARY_WEAPON_BUY_COUNT ] = { ... };
static BuyInfo secondaryWeaponBuyInfoCT[ SECONDARY_WEAPON_BUY_COUNT ] = { ... };
static BuyInfo primaryWeaponBuyInfoT[ PRIMARY_WEAPON_BUY_COUNT ] = { ... };
static BuyInfo secondaryWeaponBuyInfoT[ SECONDARY_WEAPON_BUY_COUNT ] = { ... };
inline WeaponType GetWeaponType( const char *alias ) { ... }
*/


//--------------------------------------------------------------------------------------------------------------
void BuyState::OnUpdate( CFFBot *me )
{
	if (!me || !TheNavMesh || !FFGameRules() || !TheFFBots() || !me->GetProfile()) return; // Null checks

	char cmdBuffer[256];

	if (!TheNavMesh->IsLoaded()) return;

	if (m_isInitialDelay)
	{
		if (gpGlobals->curtime - me->GetStateTimestamp() < 0.25f) return;
		m_isInitialDelay = false;
	}

	if (m_doneBuying)
	{
		if (FFGameRules()->IsMultiplayer() && FFGameRules()->IsFreezePeriod())
		{
			me->EquipBestWeapon( true ); // Was MUST_EQUIP
			me->Reload();
			me->ResetStuckMonitor();
			return;
		}
		me->Idle();
		return;
	}

	const char *cheatWeaponString = bot_loadout.GetString();
	if ( cheatWeaponString && *cheatWeaponString )
	{
		// ... (bot_loadout cheat logic, needs FF item names and GiveDefuser/HasNightVision equivalents) ...
		m_doneBuying = true;
		return;
	}

	if (!me->IsInBuyZone())
	{
		m_doneBuying = true;
		// CONSOLE_ECHO( "%s bot spawned outside of a buy zone (%d, %d, %d)\n", ... ); // TODO_FF: Team names
		return;
	}

	if (gpGlobals->curtime - me->GetStateTimestamp() > 0.02f) // buyInterval
	{
		me->m_stateTimestamp = gpGlobals->curtime;
		bool isPreferredAllDisallowed = true;

		// TODO_FF: This whole weapon buying logic needs to be overhauled for FF.
		// It should use g_FFBotWeaponTranslations and WeaponIDToAliasFF/GetWeaponClassTypeFF.
		// The current logic uses CS-specific BuyInfo tables, WeaponType enum, and CS WeaponIDs.

		// Placeholder for preferred weapon buy
		if (m_prefIndex < me->GetProfile()->GetWeaponPreferenceCount() && !bot_randombuy.GetBool() )
		{
			// ... (CS preferred weapon buy logic) ...
			// Example of how it might look with FF utilities:
			// FFWeaponID prefID = (FFWeaponID)me->GetProfile()->GetWeaponPreference(m_prefIndex);
			// const char* alias = WeaponIDToAliasFF(prefID);
			// if (alias && IsWeaponAllowedByConvars(prefID)) { /* buy */ }
			m_prefIndex = 9999; // Force skip for now
			return;
		}

		// Placeholder for random primary weapon buy
		if (!me->HasPrimaryWeapon() && (isPreferredAllDisallowed || !me->GetProfile()->HasPrimaryPreference()))
		{
			// ... (CS random primary weapon buy logic) ...
			// Example: iterate g_FFBotWeaponTranslations, check if allowed by convars and class, pick one.
		}

		if (me->HasPrimaryWeapon() || m_retries++ > 5)
		{
			// ... (CS ammo, armor, pistol, grenade, defuser buy logic) ...
			// All this needs FF equivalents.
			m_doneBuying = true;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuyState::OnExit( CFFBot *me )
{
	if (!me) return;
	me->ResetStuckMonitor();
	me->EquipBestWeapon();
}
