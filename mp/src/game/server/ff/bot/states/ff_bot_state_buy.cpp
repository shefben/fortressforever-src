//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "../../../shared/ff/ff_gamerules.h" // For FFGameRules() - TODO: Update if CSGameRules specific features are needed and have FF equivalents
#include "ff_bot_state_buy.h"
#include "../ff_bot.h" // For CFFBot methods called by the state
#include "../ff_bot_manager.h" // For TheFFBots()
#include "../../ff_player.h" // For CFFPlayer
#include "../../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase, FFWeaponID
#include "../../../shared/ff/weapons/ff_weapon_parse.h"  // For CFFWeaponInfo, BotProfileManager
#include "../ff_gamestate.h" // For FFGameState (potentially used by CFFBot)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//--------------------------------------------------------------------------------------------------------------
ConVar bot_loadout( "bot_loadout", "", FCVAR_CHEAT, "bots are given these items at round start" );
ConVar bot_randombuy( "bot_randombuy", "0", FCVAR_CHEAT, "should bots ignore their prefered weapons and just buy weapons at random?" );

//--------------------------------------------------------------------------------------------------------------
// Copied from CFFBot class in ff_bot.cpp, as it's used by BuyState
// TODO: Consider moving this to a utility file if used by other states too.
// TODO: This function needs to be updated to use FF weapon IDs and team definitions correctly.
static bool HasDefaultPistol( CFFBot *me )
{
	CFFWeaponBase *pistol = (CFFWeaponBase *)me->Weapon_GetSlot( WEAPON_SLOT_PISTOL );

	if (pistol == NULL)
		return false;

	// These specific weapon checks might need to be updated for Fortress Forever
	// Assuming WEAPON_GLOCK and WEAPON_USP are placeholders for FF default pistols
	// This should use FFWeaponID enum and potentially BotProfileManager::GetWeaponInfo if that's still relevant
	// For now, using direct FFWeaponID values if known, or placeholder logic.
	// This is highly dependent on how default pistols are defined in FF.
	// Example:
	// if (me->GetTeamNumber() == TEAM_T_STANDARD && pistol->GetWeaponID() == FF_WEAPON_DEFAULT_T_PISTOL )
	//	 return true;
	// if (me->GetTeamNumber() == TEAM_CT_STANDARD && pistol->GetWeaponID() == FF_WEAPON_DEFAULT_CT_PISTOL )
	//	 return true;


	// Placeholder logic from CS, likely incorrect for FF:
	const CFFWeaponInfo *glockInfo = BotProfileManager::GetWeaponInfo( WEAPON_GLOCK ); // WEAPON_GLOCK is CS specific
	const CFFWeaponInfo *uspInfo = BotProfileManager::GetWeaponInfo( WEAPON_USP );       // WEAPON_USP is CS specific

	if (glockInfo && me->GetTeamNumber() == TEAM_TERRORIST && pistol->GetWeaponID() == glockInfo->m_weaponId )
		return true;

	if (uspInfo && me->GetTeamNumber() == TEAM_CT && pistol->GetWeaponID() == uspInfo->m_weaponId )
		return true;

	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Buy weapons, armor, etc.
 */
void BuyState::OnEnter( CFFBot *me )
{
	m_retries = 0;
	m_prefRetries = 0;
	m_prefIndex = 0;

	const char *cheatWeaponString = bot_loadout.GetString();
	if ( cheatWeaponString && *cheatWeaponString )
	{
		m_doneBuying = false; // we're going to be given weapons - ignore the eco limit
	}
	else
	{
		// check if we are saving money for the next round
		if (me->m_iAccount < cv_bot_eco_limit.GetFloat())
		{
			me->PrintIfWatched( "Saving money for next round.\n" );
			m_doneBuying = true;
		}
		else
		{
			m_doneBuying = false;
		}
	}

	m_isInitialDelay = true;

	// this will force us to stop holding live grenade
	me->EquipBestWeapon( MUST_EQUIP );

	m_buyDefuseKit = false;
	m_buyShield = false;

	if (me->GetTeamNumber() == TEAM_CT) // TODO: Update for FF Teams if necessary
	{
		if (TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_DEFUSE_BOMB) // TODO: Update for FF Scenarios
		{
			// CT's sometimes buy defuse kits in the bomb scenario (except in career mode, where the player should defuse)
			// TODO: Update FFGameRules() if a Fortress Forever equivalent exists, or remove if not applicable
			if (FFGameRules() && FFGameRules()->IsCareer() == false)
			{
				const float buyDefuseKitChance = 100.0f * (me->GetProfile()->GetSkill() + 0.2f);
				if (RandomFloat( 0.0f, 100.0f ) < buyDefuseKitChance)
				{
					m_buyDefuseKit = true;
				}
			}
		}

		// determine if we want a tactical shield
		// TODO: Update for FF if shields are different or not present
		if (!me->HasPrimaryWeapon() && TheFFBots()->AllowTacticalShield())
		{
			if (me->m_iAccount > 2500)
			{
				if (me->m_iAccount < 4000)
					m_buyShield = (RandomFloat( 0, 100.0f ) < 33.3f) ? true : false;
				else
					m_buyShield = (RandomFloat( 0, 100.0f ) < 10.0f) ? true : false;
			}
		}
	}

	if (TheFFBots()->AllowGrenades())
	{
		m_buyGrenade = (RandomFloat( 0.0f, 100.0f ) < 33.3f) ? true : false;
	}
	else
	{
		m_buyGrenade = false;
	}


	m_buyPistol = false;
	if (TheFFBots()->AllowPistols())
	{
		// check if we have a pistol
		if (me->Weapon_GetSlot( WEAPON_SLOT_PISTOL ))
		{
			// if we have our default pistol, think about buying a different one
			if (HasDefaultPistol( me ))
			{
				// if everything other than pistols is disallowed, buy a pistol
				if (TheFFBots()->AllowShotguns() == false &&
					TheFFBots()->AllowSubMachineGuns() == false &&
					TheFFBots()->AllowRifles() == false &&
					TheFFBots()->AllowMachineGuns() == false &&
					TheFFBots()->AllowTacticalShield() == false && // TODO: Update for FF
					TheFFBots()->AllowSnipers() == false)
				{
					m_buyPistol = (RandomFloat( 0, 100 ) < 75.0f);
				}
				else if (me->m_iAccount < 1000)
				{
					// if we're low on cash, buy a pistol
					m_buyPistol = (RandomFloat( 0, 100 ) < 75.0f);
				}
				else
				{
					m_buyPistol = (RandomFloat( 0, 100 ) < 33.3f);
				}
			}
		}
		else
		{
			// we dont have a pistol - buy one
			m_buyPistol = true;
		}
	}
}


enum WeaponType
{
	PISTOL,
	SHOTGUN,
	SUB_MACHINE_GUN,
	RIFLE,
	MACHINE_GUN,
	SNIPER_RIFLE,
	GRENADE,

	NUM_WEAPON_TYPES
};

struct BuyInfo
{
	WeaponType type;
	bool preferred;			///< more challenging bots prefer these weapons
	const char *buyAlias;			///< the buy alias for this equipment
};

#define PRIMARY_WEAPON_BUY_COUNT 13
#define SECONDARY_WEAPON_BUY_COUNT 3

/**
 * These tables MUST be kept in sync with the CT and T buy aliases
 * TODO: Update these tables for Fortress Forever weapons and aliases
 */

static BuyInfo primaryWeaponBuyInfoCT[ PRIMARY_WEAPON_BUY_COUNT ] =
{
	{ SHOTGUN,			false, "m3" },			// WEAPON_M3
	{ SHOTGUN,			false, "xm1014" },		// WEAPON_XM1014
	{ SUB_MACHINE_GUN,	false, "tmp" },			// WEAPON_TMP
	{ SUB_MACHINE_GUN,	false, "mp5navy" },			// WEAPON_MP5N
	{ SUB_MACHINE_GUN,	false, "ump45" },		// WEAPON_UMP45
	{ SUB_MACHINE_GUN,	false, "p90" },			// WEAPON_P90
	{ RIFLE,			true,  "famas" },		// WEAPON_FAMAS
	{ SNIPER_RIFLE,		false, "scout" },		// WEAPON_SCOUT
	{ RIFLE,			true,  "m4a1" },		// WEAPON_M4A1
	{ RIFLE,			false, "aug" },			// WEAPON_AUG
	{ SNIPER_RIFLE,		true,  "sg550" },		// WEAPON_SG550
	{ SNIPER_RIFLE,		true,  "awp" },			// WEAPON_AWP
	{ MACHINE_GUN,		false, "m249" }			// WEAPON_M249
};

static BuyInfo secondaryWeaponBuyInfoCT[ SECONDARY_WEAPON_BUY_COUNT ] =
{
//	{ PISTOL,	false,	"glock" },
//	{ PISTOL,	false,	"usp" },
	{ PISTOL, true,		"p228" },
	{ PISTOL, true,		"deagle" },
	{ PISTOL, true,		"fn57" }
};


static BuyInfo primaryWeaponBuyInfoT[ PRIMARY_WEAPON_BUY_COUNT ] =
{
	{ SHOTGUN,			false, "m3" },			// WEAPON_M3
	{ SHOTGUN,			false, "xm1014" },		// WEAPON_XM1014
	{ SUB_MACHINE_GUN,	false, "mac10" },		// WEAPON_MAC10
	{ SUB_MACHINE_GUN,	false, "mp5navy" },			// WEAPON_MP5N
	{ SUB_MACHINE_GUN,	false, "ump45" },		// WEAPON_UMP45
	{ SUB_MACHINE_GUN,	false, "p90" },			// WEAPON_P90
	{ RIFLE,			true,  "galil" },		// WEAPON_GALIL
	{ RIFLE,			true,  "ak47" },		// WEAPON_AK47
	{ SNIPER_RIFLE,		false, "scout" },		// WEAPON_SCOUT
	{ RIFLE,			true,  "sg552" },		// WEAPON_SG552
	{ SNIPER_RIFLE,		true,  "awp" },			// WEAPON_AWP
	{ SNIPER_RIFLE,		true,  "g3sg1" },		// WEAPON_G3SG1
	{ MACHINE_GUN,		false, "m249" }			// WEAPON_M249
};

static BuyInfo secondaryWeaponBuyInfoT[ SECONDARY_WEAPON_BUY_COUNT ] =
{
//	{ PISTOL,	false,	"glock" },
//	{ PISTOL,	false,	"usp" },
	{ PISTOL, true,		"p228" },
	{ PISTOL, true,		"deagle" },
	{ PISTOL, true,		"elites" }
};

/**
 * Given a weapon alias, return the kind of weapon it is
 * TODO: Update for Fortress Forever weapon aliases and types
 */
inline WeaponType GetWeaponType( const char *alias )
{
	int i;

	for( i=0; i<PRIMARY_WEAPON_BUY_COUNT; ++i )
	{
		if (!stricmp( alias, primaryWeaponBuyInfoCT[i].buyAlias ))
			return primaryWeaponBuyInfoCT[i].type;

		if (!stricmp( alias, primaryWeaponBuyInfoT[i].buyAlias ))
			return primaryWeaponBuyInfoT[i].type;
	}

	for( i=0; i<SECONDARY_WEAPON_BUY_COUNT; ++i )
	{
		if (!stricmp( alias, secondaryWeaponBuyInfoCT[i].buyAlias ))
			return secondaryWeaponBuyInfoCT[i].type;

		if (!stricmp( alias, secondaryWeaponBuyInfoT[i].buyAlias ))
			return secondaryWeaponBuyInfoT[i].type;
	}

	return NUM_WEAPON_TYPES;
}




//--------------------------------------------------------------------------------------------------------------
void BuyState::OnUpdate( CFFBot *me )
{
	char cmdBuffer[256];

	// wait for a Navigation Mesh
	if (!TheNavMesh->IsLoaded())
		return;

	// apparently we cant buy things in the first few seconds, so wait a bit
	if (m_isInitialDelay)
	{
		const float waitToBuyTime = 0.25f;
		if (gpGlobals->curtime - me->GetStateTimestamp() < waitToBuyTime)
			return;

		m_isInitialDelay = false;
	}

	// if we're done buying and still in the freeze period, wait
	if (m_doneBuying)
	{
		// TODO: Update CSGameRules() if a Fortress Forever equivalent exists, or remove if not applicable
		if (FFGameRules() && FFGameRules()->IsMultiplayer() && FFGameRules()->IsFreezePeriod())
		{
			// make sure we're locked and loaded
			me->EquipBestWeapon( MUST_EQUIP );
			me->Reload();
			me->ResetStuckMonitor();
			return;
		}

		me->Idle();
		return;
	}

	// If we're supposed to buy a specific weapon for debugging, do so and then bail
	const char *cheatWeaponString = bot_loadout.GetString();
	if ( cheatWeaponString && *cheatWeaponString )
	{
		CUtlVector<char*, CUtlMemory<char*> > loadout;
		Q_SplitString( cheatWeaponString, " ", loadout );
		for ( int i=0; i<loadout.Count(); ++i )
		{
			const char *item = loadout[i];
			if ( FStrEq( item, "vest" ) )
			{
				me->GiveNamedItem( "item_kevlar" );
			}
			else if ( FStrEq( item, "vesthelm" ) )
			{
				me->GiveNamedItem( "item_assaultsuit" );
			}
			else if ( FStrEq( item, "defuser" ) )
			{
				if ( me->GetTeamNumber() == TEAM_CT ) // TODO: Update for FF Teams if necessary
				{
					// me->GiveDefuser(); // TODO: May need FF equivalent for GiveDefuser
				}
			}
			else if ( FStrEq( item, "nvgs" ) )
			{
				// me->m_bHasNightVision = true; // TODO: May need FF equivalent for NVGs
			}
			else if ( FStrEq( item, "primammo" ) )
			{
				me->AttemptToBuyAmmo( 0 );
			}
			else if ( FStrEq( item, "secammo" ) )
			{
				me->AttemptToBuyAmmo( 1 );
			}
			else
			{
				me->GiveWeapon( item );
			}
		}
		m_doneBuying = true;
		return;
	}


	if (!me->IsInBuyZone())
	{
		m_doneBuying = true;
		CONSOLE_ECHO( "%s bot spawned outside of a buy zone (%d, %d, %d)\n",
						(me->GetTeamNumber() == TEAM_CT) ? "CT" : "Terrorist", // TODO: Update team names if necessary
						(int)me->GetAbsOrigin().x,
						(int)me->GetAbsOrigin().y,
						(int)me->GetAbsOrigin().z );
		return;
	}

	// try to buy some weapons
	const float buyInterval = 0.02f;
	if (gpGlobals->curtime - me->GetStateTimestamp() > buyInterval)
	{
		me->m_stateTimestamp = gpGlobals->curtime;

		bool isPreferredAllDisallowed = true;

		// try to buy our preferred weapons first
		if (m_prefIndex < me->GetProfile()->GetWeaponPreferenceCount() && bot_randombuy.GetBool() == false )
		{
			// need to retry because sometimes first buy fails??
			const int maxPrefRetries = 2;
			if (m_prefRetries >= maxPrefRetries)
			{
				// try to buy next preferred weapon
				++m_prefIndex;
				m_prefRetries = 0;
				return;
			}

			int weaponPreference = me->GetProfile()->GetWeaponPreference( m_prefIndex );

			// don't buy it again if we still have one from last round
			char weaponPreferenceName[32];
			Q_snprintf( weaponPreferenceName, sizeof(weaponPreferenceName), "weapon_%s", me->GetProfile()->GetWeaponPreferenceAsString( m_prefIndex ) );
			if( me->Weapon_OwnsThisType(weaponPreferenceName) )//Prefs and buyalias use the short version, this uses the long
			{
				// done with buying preferred weapon
				m_prefIndex = 9999;
				return;
			}

			// TODO: Update WEAPON_SHIELDGUN if it's not in FF (likely not relevant)
			// if (me->HasShield() && weaponPreference == WEAPON_SHIELDGUN)
			// {
			//	 // done with buying preferred weapon
			//	 m_prefIndex = 9999;
			//	 return;
			// }

			const char *buyAlias = NULL;

			// TODO: Update WEAPON_SHIELDGUN logic if shields are different/not in FF
			// if (weaponPreference == WEAPON_SHIELDGUN)
			// {
			// 	if (TheFFBots()->AllowTacticalShield())
			// 		buyAlias = "shield";
			// }
			// else
			{
				buyAlias = WeaponIDToAlias( weaponPreference ); // TODO: Ensure WeaponIDToAlias works for FF weapons
				WeaponType type = GetWeaponType( buyAlias ); // TODO: Ensure GetWeaponType works for FF
				switch( type )
				{
					case PISTOL:
						if (!TheFFBots()->AllowPistols())
							buyAlias = NULL;
						break;

					case SHOTGUN:
						if (!TheFFBots()->AllowShotguns())
							buyAlias = NULL;
						break;

					case SUB_MACHINE_GUN:
						if (!TheFFBots()->AllowSubMachineGuns())
							buyAlias = NULL;
						break;

					case RIFLE:
						if (!TheFFBots()->AllowRifles())
							buyAlias = NULL;
						break;

					case MACHINE_GUN:
						if (!TheFFBots()->AllowMachineGuns())
							buyAlias = NULL;
						break;

					case SNIPER_RIFLE:
						if (!TheFFBots()->AllowSnipers())
							buyAlias = NULL;
						break;
				}
			}

			if (buyAlias)
			{
				Q_snprintf( cmdBuffer, 256, "buy %s\n", buyAlias );

				CCommand args;
				args.Tokenize( cmdBuffer );
				me->ClientCommand( args );

				me->PrintIfWatched( "Tried to buy preferred weapon %s.\n", buyAlias );
				isPreferredAllDisallowed = false;
			}

			++m_prefRetries;

			// bail out so we dont waste money on other equipment
			// unless everything we prefer has been disallowed, then buy at random
			if (isPreferredAllDisallowed == false)
				return;
		}

		// if we have no preferred primary weapon (or everything we want is disallowed), buy at random
		if (!me->HasPrimaryWeapon() && (isPreferredAllDisallowed || !me->GetProfile()->HasPrimaryPreference()))
		{
			if (m_buyShield) // TODO: Update for FF if shields are different or not present
			{
				// buy a shield
				CCommand args;
				// args.Tokenize( "buy shield" ); // TODO: Update shield alias if needed
				// me->ClientCommand( args );

				me->PrintIfWatched( "Tried to buy a shield.\n" );
			}
			else
			{
				// build list of allowable weapons to buy
				// TODO: Update these weapon tables for Fortress Forever
				BuyInfo *masterPrimary = (me->GetTeamNumber() == TEAM_TERRORIST) ? primaryWeaponBuyInfoT : primaryWeaponBuyInfoCT;
				BuyInfo *stockPrimary[ PRIMARY_WEAPON_BUY_COUNT ];
				int stockPrimaryCount = 0;

				// dont choose sniper rifles as often
				const float sniperRifleChance = 50.0f;
				bool wantSniper = (RandomFloat( 0, 100 ) < sniperRifleChance) ? true : false;

				if ( bot_randombuy.GetBool() )
				{
					wantSniper = true;
				}

				for( int i=0; i<PRIMARY_WEAPON_BUY_COUNT; ++i )
				{
					if ((masterPrimary[i].type == SHOTGUN && TheFFBots()->AllowShotguns()) ||
						(masterPrimary[i].type == SUB_MACHINE_GUN && TheFFBots()->AllowSubMachineGuns()) ||
						(masterPrimary[i].type == RIFLE && TheFFBots()->AllowRifles()) ||
						(masterPrimary[i].type == SNIPER_RIFLE && TheFFBots()->AllowSnipers() && wantSniper) ||
						(masterPrimary[i].type == MACHINE_GUN && TheFFBots()->AllowMachineGuns()))
					{
						stockPrimary[ stockPrimaryCount++ ] = &masterPrimary[i];
					}
				}

				if (stockPrimaryCount)
				{
					// buy primary weapon if we don't have one
					int which;

					// on hard difficulty levels, bots try to buy preferred weapons on the first pass
					if (m_retries == 0 && TheFFBots()->GetDifficultyLevel() >= BOT_HARD && bot_randombuy.GetBool() == false )
					{
						// count up available preferred weapons
						int prefCount = 0;
						for( which=0; which<stockPrimaryCount; ++which )
							if (stockPrimary[which]->preferred)
								++prefCount;

						if (prefCount)
						{
							int whichPref = RandomInt( 0, prefCount-1 );
							for( which=0; which<stockPrimaryCount; ++which )
								if (stockPrimary[which]->preferred && whichPref-- == 0)
									break;
						}
						else
						{
							// no preferred weapons available, just pick randomly
							which = RandomInt( 0, stockPrimaryCount-1 );
						}
					}
					else
					{
						which = RandomInt( 0, stockPrimaryCount-1 );
					}

					Q_snprintf( cmdBuffer, 256, "buy %s\n", stockPrimary[ which ]->buyAlias );

					CCommand args;
					args.Tokenize( cmdBuffer );
					me->ClientCommand( args );

					me->PrintIfWatched( "Tried to buy %s.\n", stockPrimary[ which ]->buyAlias );
				}
			}
		}


		//
		// If we now have a weapon, or have tried for too long, we're done
		//
		if (me->HasPrimaryWeapon() || m_retries++ > 5)
		{
			// primary ammo
			CCommand args;
			if (me->HasPrimaryWeapon())
			{
				args.Tokenize( "buy primammo" );
				me->ClientCommand( args );
			}

			// buy armor last, to make sure we bought a weapon first
			// args.Tokenize( "buy vesthelm" ); // TODO: Update armor aliases if needed for FF
			// me->ClientCommand( args );
			// args.Tokenize( "buy vest" );
			// me->ClientCommand( args );

			// pistols - if we have no preferred pistol, buy at random
			if (TheFFBots()->AllowPistols() && !me->GetProfile()->HasPistolPreference())
			{
				if (m_buyPistol)
				{
					int which = RandomInt( 0, SECONDARY_WEAPON_BUY_COUNT-1 );

					const char *what = NULL;
					// TODO: Update these weapon tables for Fortress Forever
					if (me->GetTeamNumber() == TEAM_TERRORIST)
						what = secondaryWeaponBuyInfoT[ which ].buyAlias;
					else
						what = secondaryWeaponBuyInfoCT[ which ].buyAlias;

					Q_snprintf( cmdBuffer, 256, "buy %s\n", what );
					args.Tokenize( cmdBuffer );
					me->ClientCommand( args );


					// only buy one pistol
					m_buyPistol = false;
				}

				// make sure we have enough pistol ammo
				args.Tokenize( "buy secammo" );
				me->ClientCommand( args );
			}

			// buy a grenade if we wish, and we don't already have one
			if (m_buyGrenade && !me->HasGrenade()) // TODO: Update HasGrenade for FF if necessary
			{
				// TODO: Update grenade types/aliases for FF. FF has different grenade types.
				// This logic will need significant changes based on FF's grenade system.
				// Example: FF might have frag, emp, concussion, etc.
				// if (UTIL_IsTeamAllBots( me->GetTeamNumber() ))
				// {
				// 	// only allow Flashbangs if everyone on the team is a bot (dont want to blind our friendly humans)
				// 	float rnd = RandomFloat( 0, 100 );

				// 	if (rnd < 10)
				// 	{
				// 		args.Tokenize( "buy smokegrenade" );
				// 		me->ClientCommand( args );	// smoke grenade
				// 	}
				// 	else if (rnd < 35)
				// 	{
				// 		args.Tokenize( "buy flashbang" );
				// 		me->ClientCommand( args );	// flashbang
				// 	}
				// 	else
				// 	{
				// 		args.Tokenize( "buy hegrenade" );
				// 		me->ClientCommand( args );	// he grenade
				// 	}
				// }
				// else
				// {
				// 	if (RandomFloat( 0, 100 ) < 10)
				// 	{
				// 		args.Tokenize( "buy smokegrenade" );	// smoke grenade
				// 		me->ClientCommand( args );
				// 	}
				// 	else
				// 	{
				// 		args.Tokenize( "buy hegrenade" );	// he grenade
				// 		me->ClientCommand( args );
				// 	}
				// }
			}

			if (m_buyDefuseKit) // TODO: Update for FF if defuse kits are different or not present
			{
				// args.Tokenize( "buy defuser" ); // TODO: Update defuser alias if needed
				// me->ClientCommand( args );
			}

			m_doneBuying = true;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void BuyState::OnExit( CFFBot *me )
{
	me->ResetStuckMonitor();
	me->EquipBestWeapon();
}
