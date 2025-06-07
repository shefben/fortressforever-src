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
#include "ff_buildableobject.h" // For CFFBuildableObject
#include "items.h" // For CItem, assuming ammo packs might be derived from it

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

const int SENTRY_COST_CELLS = 130;
const int DISPENSER_COST_CELLS = 100;
// These were previously extern in build states, now defined here for clarity.
// Ideally, these would be in a shared game constants header.

LINK_ENTITY_TO_CLASS( ff_bot, CFFBot );

BEGIN_DATADESC( CFFBot )
	// DEFINE_FIELD( m_isRogue, FIELD_BOOLEAN ), // Example if we were saving this
END_DATADESC()

//--------------------------------------------------------------------------------------------------------------
CFFBot::CFFBot()
{
	// FF_TODO: Initialize any FF-specific members
	m_fireWeaponTimestamp = 0.0f;
	m_isRapidFiring = false;
	m_zoomTimer.Invalidate();
	// Many CS-specific initializations from CCSBot::CCSBot() are omitted here as they relate to
	// bomb, hostages, shield, etc. or have FF equivalents handled elsewhere (like m_gameState).
}

CFFBot::~CFFBot()
{
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

	if (attacker && attacker->IsPlayer()) // Added null check for attacker
	{
		CFFPlayer *player = static_cast<CFFPlayer *>( attacker );
		if (InSameTeam( player ) && !player->IsBot()) GetChatter()->FriendlyFire();
	}

	if (attacker && attacker->IsPlayer() && IsEnemy( attacker ))  // Added null check for attacker
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
void CFFBot::Event_Killed( const CTakeDamageInfo &info )
{ 
	GetChatter()->OnDeath();
	const float deathDanger = 1.0f;
	const float deathDangerRadius = 500.0f;
	if (m_lastKnownArea) // Added null check
	{
		TheNavMesh->IncreaseDangerNearby( GetTeamNumber(), deathDanger, m_lastKnownArea, GetAbsOrigin(), deathDangerRadius );
	}
	m_voiceEndTimestamp = 0.0f;

	// Holster weapon
	CFFWeaponBase *gun = GetActiveFFWeapon(); // FF_WEAPONS
	if (gun)
	{
		gun->Holster( NULL );
	}

	BaseClass::Event_Killed( info );
}

//--------------------------------------------------------------------------------------------------------------
#define HI_X	0x01
#define LO_X 0x02
#define HI_Y	0x04
#define LO_Y 0x08
#define HI_Z	0x10
#define LO_Z 0x20
inline bool IsIntersectingBox( const Vector& start, const Vector& end, const Vector& boxMin, const Vector& boxMax )
{
	unsigned char startFlags = 0, endFlags = 0;
	if (start.x < boxMin.x) startFlags |= LO_X; if (start.x > boxMax.x) startFlags |= HI_X;
	if (start.y < boxMin.y) startFlags |= LO_Y; if (start.y > boxMax.y) startFlags |= HI_Y;
	if (start.z < boxMin.z) startFlags |= LO_Z; if (start.z > boxMax.z) startFlags |= HI_Z;
	if (end.x < boxMin.x) endFlags |= LO_X; if (end.x > boxMax.x) endFlags |= HI_X;
	if (end.y < boxMin.y) endFlags |= LO_Y; if (end.y > boxMax.y) endFlags |= HI_Y;
	if (end.z < boxMin.z) endFlags |= LO_Z; if (end.z > boxMax.z) endFlags |= HI_Z;
	if (startFlags & endFlags) return false;
	return true;
}
extern void UTIL_DrawBox( Extent *extent, int lifetime, int red, int green, int blue );

//--------------------------------------------------------------------------------------------------------------
void CFFBot::Touch( CBaseEntity *other )
{
	BaseClass::Touch( other );
	if (other->IsPlayer())
	{
		if (IsUsingLadder()) return;
		CFFPlayer *player = static_cast<CFFPlayer *>( other );
		unsigned int otherPri = TheFFBots()->GetPlayerPriority( player );
		unsigned int myPri = TheFFBots()->GetPlayerPriority( this );
		if (myPri < otherPri) return;
		if (m_avoid != NULL)
		{
			CBasePlayer* pAvoidPlayer = dynamic_cast<CBasePlayer*>(m_avoid.Get());
			if (pAvoidPlayer) { if (TheFFBots()->GetPlayerPriority( pAvoidPlayer ) < otherPri) return; }
		}
		m_avoid = other;
		m_avoidTimestamp = gpGlobals->curtime;
	}
	if ( !m_isStuck && !IsCrouching() && !IsOnLadder() ) return;
	if ( IsBreakableEntity( other ) ) SetLookAt( "Breakable", other->WorldSpaceCenter(), PRIORITY_HIGH, 0.1f, false, 5.0f, true );
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsBusy( void ) const
{
	// FF specific busy conditions may go here
	if (IsAttacking() || IsBuying() || IsSniping()) return true; // IsBuying might need FF specific logic
	return false;
}
//--------------------------------------------------------------------------------------------------------------
void CFFBot::BotDeathThink( void ) { }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToJoinTeam( int team ) { m_desiredTeam = team; }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::SetBotEnemy( CFFPlayer *enemy )
{
	if (m_enemy.Get() != enemy)
	{
		m_enemy = enemy; 
		m_currentEnemyAcquireTimestamp = gpGlobals->curtime;
		PrintIfWatched( "SetBotEnemy: %s\n", (enemy) ? enemy->GetPlayerName() : "(NULL)" );
	}
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::StayOnNavMesh( void )
{
	if (m_currentArea == NULL)
	{
		CNavArea *goalArea = NULL;
		if (!m_lastKnownArea)
		{
			goalArea = TheNavMesh->GetNearestNavArea( GetCentroid( this ) );
			PrintIfWatched( "Started off the nav mesh - moving to closest nav area...\n" );
		}
		else { goalArea = m_lastKnownArea; PrintIfWatched( "Getting out of NULL area...\n" ); }
		if (goalArea)
		{
			Vector pos; goalArea->GetClosestPointOnArea( GetCentroid( this ), &pos );
			Vector to = pos - GetCentroid( this ); to.NormalizeInPlace();
			const float stepInDist = 5.0f;	pos = pos + (stepInDist * to);
			MoveTowardsPosition( pos );
		}
		if (m_isStuck) Wiggle(); return false;
	}
	return true;
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsDoingScenario( void ) const
{
	if (cv_bot_defer_to_human.GetBool()){ if (UTIL_HumansOnTeam( GetTeamNumber(), IS_ALIVE )) return false; }
	return true;
}
//--------------------------------------------------------------------------------------------------------------
CFFPlayer *CFFBot::GetAttacker( void ) const { if (m_attacker && m_attacker->IsAlive()) return m_attacker.Get(); return NULL; }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::GetOffLadder( void ) { if (IsUsingLadder()) { Jump( MUST_JUMP ); DestroyPath(); } }
//--------------------------------------------------------------------------------------------------------------
float CFFBot::GetHidingSpotCheckTimestamp( HidingSpot *spot ) const
{
	for( int i=0; i<m_checkedHidingSpotCount; ++i ) if (m_checkedHidingSpot[i].spot->GetID() == spot->GetID()) return m_checkedHidingSpot[i].timestamp;
	return -999999.9f;
}
//--------------------------------------------------------------------------------------------------------------
void CFFBot::SetHidingSpotCheckTimestamp( HidingSpot *spot )
{
	int leastRecent = 0; float leastRecentTime = gpGlobals->curtime + 1.0f;
	for( int i=0; i<m_checkedHidingSpotCount; ++i )
	{
		if (m_checkedHidingSpot[i].spot->GetID() == spot->GetID()) { m_checkedHidingSpot[i].timestamp = gpGlobals->curtime; return; }
		if (m_checkedHidingSpot[i].timestamp < leastRecentTime) { leastRecentTime = m_checkedHidingSpot[i].timestamp; leastRecent = i; }
	}
	if (m_checkedHidingSpotCount < MAX_CHECKED_SPOTS)
	{
		m_checkedHidingSpot[ m_checkedHidingSpotCount ].spot = spot;
		m_checkedHidingSpot[ m_checkedHidingSpotCount ].timestamp = gpGlobals->curtime;
		++m_checkedHidingSpotCount;
	} else { m_checkedHidingSpot[ leastRecent ].spot = spot; m_checkedHidingSpot[ leastRecent ].timestamp = gpGlobals->curtime; }
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsOutnumbered( void ) const { return (GetNearbyFriendCount() < GetNearbyEnemyCount()-1); }
//--------------------------------------------------------------------------------------------------------------
int CFFBot::OutnumberedCount( void ) const { if (IsOutnumbered()) return (GetNearbyEnemyCount()-1) - GetNearbyFriendCount(); return 0; }
//--------------------------------------------------------------------------------------------------------------
CFFPlayer *CFFBot::GetImportantEnemy( bool checkVisibility ) const
{
	CFFBotManager *ctrl = TheFFBots(); CFFPlayer *nearEnemy = NULL; float nearDist = 999999999.9f;
	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBaseEntity *entity = UTIL_PlayerByIndex( i ); if (entity == NULL || !entity->IsPlayer()) continue;
		CFFPlayer *player = static_cast<CFFPlayer *>( entity );
		if (!player->IsAlive() || InSameTeam( player ) || (ctrl && !ctrl->IsImportantPlayer( player ))) continue; // Added null check for ctrl
		Vector d = GetAbsOrigin() - player->GetAbsOrigin(); float distSq = d.LengthSqr();
		if (distSq < nearDist) { if (checkVisibility && !IsVisible( player, CHECK_FOV )) continue; nearEnemy = player; nearDist = distSq; }
	} return nearEnemy;
}
//--------------------------------------------------------------------------------------------------------------
void CFFBot::SetDisposition( DispositionType disposition ) { m_disposition = disposition; if (m_disposition != IGNORE_ENEMIES) m_ignoreEnemiesTimer.Invalidate(); }
//--------------------------------------------------------------------------------------------------------------
CFFBot::DispositionType CFFBot::GetDisposition( void ) const { if (!m_ignoreEnemiesTimer.IsElapsed()) return IGNORE_ENEMIES; return m_disposition; }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::IgnoreEnemies( float duration ) { m_ignoreEnemiesTimer.Start( duration ); }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::IncreaseMorale( void ) { if (m_morale < EXCELLENT) m_morale = static_cast<MoraleType>( m_morale + 1 ); }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::DecreaseMorale( void ) { if (m_morale > TERRIBLE) m_morale = static_cast<MoraleType>( m_morale - 1 ); }
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsRogue( void ) const
{
	CFFBotManager *ctrl = TheFFBots(); if (!ctrl || !ctrl->AllowRogues()) return false;
	if (m_rogueTimer.IsElapsed())
	{
		m_rogueTimer.Start( RandomFloat( 10.0f, 30.0f ) );
		const float rogueChance = 100.0f * (1.0f - GetProfile()->GetTeamwork());
		m_isRogue = (RandomFloat( 0, 100 ) < rogueChance);
	} return m_isRogue;
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsHurrying( void ) const
{
	if (!m_hurryTimer.IsElapsed()) return true;
	return false;
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsSafe( void ) const { CFFBotManager *ctrl = TheFFBots(); return (ctrl && ctrl->GetElapsedRoundTime() < m_safeTime); }
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsWellPastSafe( void ) const { CFFBotManager *ctrl = TheFFBots(); return (ctrl && ctrl->GetElapsedRoundTime() > 2.0f * m_safeTime); }
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsEndOfSafeTime( void ) const { return m_wasSafe && !IsSafe(); }
//--------------------------------------------------------------------------------------------------------------
float CFFBot::GetSafeTimeRemaining( void ) const { CFFBotManager *ctrl = TheFFBots(); return m_safeTime - (ctrl ? ctrl->GetElapsedRoundTime() : 0.0f); }
//--------------------------------------------------------------------------------------------------------------
void CFFBot::AdjustSafeTime( void )
{
	CFFBotManager *ctrl = TheFFBots();
	if (ctrl && ctrl->GetElapsedRoundTime() < m_safeTime) { m_safeTime = ctrl->GetElapsedRoundTime() - 2.0f; }
}
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::HasNotSeenEnemyForLongTime( void ) const { const float longTime = 30.0f; return (GetTimeSinceLastSawEnemy() > longTime); }
//--------------------------------------------------------------------------------------------------------------
bool CFFBot::GuardRandomZone( float range )
{
	CFFBotManager *ctrl = TheFFBots();
	const CFFBotManager::Zone *zone = ctrl ? ctrl->GetRandomZone() : NULL;
	if (zone)
	{
		CNavArea *area = ctrl->GetRandomAreaInZone( zone );
		if (area) { Hide( area, -1.0f, range ); return true; }
	} return false;
}
//--------------------------------------------------------------------------------------------------------------
const char *CFFBot::GetTaskName( void ) const
{
	static const char *name[ NUM_TASKS ] =
	{
		"SEEK_AND_DESTROY", "CAPTURE_FLAG", "DEFEND_POINT", "PUSH_CART", "GUARD_OBJECTIVE",
		"GUARD_INITIAL_ENCOUNTER", "HOLD_POSITION", "FOLLOW", "MOVE_TO_LAST_KNOWN_ENEMY_POSITION",
		"MOVE_TO_SNIPER_SPOT", "SNIPING",
	};
	int taskIndex = (int)GetTask();
	if ( taskIndex >= 0 && taskIndex < NUM_TASKS ) return name[ taskIndex ];
	return "UNKNOWN_TASK";
}
//--------------------------------------------------------------------------------------------------------------
const char *CFFBot::GetDispositionName( void ) const
{
	static const char *name[ NUM_DISPOSITIONS ] = { "ENGAGE_AND_INVESTIGATE", "OPPORTUNITY_FIRE", "SELF_DEFENSE", "IGNORE_ENEMIES" };
	return name[ (int)GetDisposition() ];
}
//--------------------------------------------------------------------------------------------------------------
const char *CFFBot::GetMoraleName( void ) const
{
	static const char *name[ EXCELLENT - TERRIBLE + 1 ] = { "TERRIBLE", "BAD", "NEGATIVE", "NEUTRAL", "POSITIVE", "GOOD", "EXCELLENT" };
	return name[ (int)GetMorale() + 3 ];
}
//--------------------------------------------------------------------------------------------------------------
void CFFBot::BuildUserCmd( CUserCmd& cmd, const QAngle& viewangles, float forwardmove, float sidemove, float upmove, int buttons, byte impulse )
{
	Q_memset( &cmd, 0, sizeof( cmd ) );
	if ( !RunMimicCommand( cmd ) )
	{
		if ( m_Local.m_bDucked || m_Local.m_bDucking ) buttons &= ~IN_SPEED;
		cmd.command_number = gpGlobals->tickcount;
		cmd.forwardmove = forwardmove; cmd.sidemove = sidemove; cmd.upmove = upmove;
		cmd.buttons = buttons; cmd.impulse = impulse;
		VectorCopy( viewangles, cmd.viewangles );
		cmd.random_seed = random->RandomInt( 0, 0x7fffffff );
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToGuardSentry()
{
	if (!IsEngineer())
		return;

	CFFSentryGun *sentry = GetSentryGun(); // Assumes this returns the bot's active sentry

	if (sentry && sentry->IsAlive() && sentry->IsBuilt()) // Check if sentry exists, is alive, and fully built
	{
		// Don't switch to GuardSentry if currently building/upgrading it or another buildable.
		if ( GetState() == &m_buildSentryState || GetState() == &m_buildDispenserState )
		{
			// If it's this very sentry that was just finished, OnExit of build state will call this.
			// If it's another buildable, let that finish first.
			// This check might need refinement based on how m_sentryBeingBuilt is cleared in build states.
			BuildSentryState* pBS = dynamic_cast<BuildSentryState*>(GetState());
			if (pBS && pBS->m_sentryBeingBuilt.Get() == sentry && (pBS->m_isBuilding || pBS->m_isUpgrading))
				return;

			BuildDispenserState* pBD = dynamic_cast<BuildDispenserState*>(GetState());
			if (pBD && (pBD->m_isBuilding || pBD->m_isUpgrading)) // If any dispenser build/upgrade is active
				return;
		}

		// Don't switch if already guarding, or repairing this sentry.
		if ( GetState() == &m_guardSentryState ||
			 (GetState() == &m_repairBuildableState && m_repairBuildableState.m_targetBuildable.Get() == sentry ) )
			return;


		PrintIfWatched("Bot %s (Engineer) is going to guard its sentry.\n", GetPlayerName());
		// The GuardSentryState will fetch the sentry itself via me->GetSentryGun() in its OnEnter.
		SetState(&m_guardSentryState);
	}
	else
	{
		PrintIfWatched("Bot %s (Engineer) has no valid sentry to guard.\n", GetPlayerName());
		// If no sentry, Idle state might decide to build one.
	}
}

//--------------------------------------------------------------------------------------------------------------
// WEAPON HANDLING METHODS (Ported and Adapted from cs_bot.cpp)
//--------------------------------------------------------------------------------------------------------------

/**
 * Returns currently equipped weapon (already in .h as inline, but if non-inline needed, it's here)
 * This is redundant with the inline in bot.h but shown for completeness of what would be in ff_bot.cpp.
 * CFFWeaponBase *CFFBot::GetActiveFFWeapon( void ) const
 * {
 *	 return static_cast<CFFWeaponBase *>(const_cast<CFFBot*>(this)->GetActiveWeapon());
 * }
 */

void CFFBot::EquipBestWeapon( bool mustEquip )
{
	// FF_TODO_WEAPONS: This logic needs a complete overhaul for FF classes and weapon systems.
	// It should consider class-specific loadouts, weapon roles (primary, secondary, melee, special),
	// ammo, and bot profile preferences.

	CFFWeaponBase *currentWeapon = GetActiveFFWeapon();

	// Don't switch away from a primed grenade (FF_TODO_WEAPONS: Adapt for FF grenade types/mechanics)
	// if (currentWeapon && currentWeapon->IsGrenade() && currentWeapon->IsPrimed()) return;

	if (currentWeapon != NULL && currentWeapon->IsWeaponVisible())
	{
		if (currentWeapon->HasAnyAmmo() && currentWeapon->Clip1() > 0)
		{
			if (!mustEquip)
				return;
		}
	}

	// FF_TODO_WEAPONS: Implement FF specific weapon selection logic.
	// This is a very naive placeholder.
	// It should iterate through available FFWeaponIDs, check if the bot has them,
	// and select based on a priority system (e.g. primary > secondary > melee).
	// The BotProfile should heavily influence this.

	// Example structure:
	// const BotProfile *profile = GetProfile();
	// if (profile)
	// {
	//		FFWeaponID preferredWep = profile->GetPreferredPrimary(); // Assuming such a method
	//		CFFWeaponBase *w = FindWeaponByID(preferredWep);
	//		if (w && w->HasAnyAmmo()) { EquipWeapon(w); return; }
	// }

	// Simple fallback: iterate all weapons and pick the first one with ammo.
	for ( int i = 0; i < MAX_WEAPONS; i++ ) // MAX_WEAPONS from CBasePlayer
	{
		CBaseCombatWeapon *pCheck = GetWeapon(i);
		if ( pCheck )
		{
			CFFWeaponBase *ffCheck = dynamic_cast<CFFWeaponBase*>(pCheck);
			if (ffCheck && ffCheck->HasAnyAmmo())
			{
				EquipWeapon(ffCheck);
				return;
			}
		}
	}
	// If no weapon with ammo, equip first available melee or any weapon
	CFFWeaponBase *meleeWep = FindInstanceOfWeapon(FF_WEAPON_KNIFE); // Example, find any class-appropriate melee
	if (!meleeWep) meleeWep = FindInstanceOfWeapon(FF_WEAPON_SPANNER);
	// ... etc for all melee types
	if (meleeWep) { EquipWeapon(meleeWep); return; }

	// If absolutely nothing else, try equipping whatever is in slot 0 (usually primary)
	CBaseCombatWeapon *fallbackWep = GetWeapon(FF_PRIMARY_WEAPON_SLOT); // FF_PRIMARY_WEAPON_SLOT
	if (fallbackWep) EquipWeapon(fallbackWep);
}

void CFFBot::EquipPistol( void )
{
	// FF_TODO_WEAPONS: Adapt for FF class-specific secondary weapons.
	// Each class might have a different "pistol" (Scout Shotgun, Engy Railgun, Spy Tranq).
	// This needs to check the bot's class and equip the appropriate secondary.
	// CFFWeaponBase *secondary = FindSecondaryWeaponForClass(GetPlayerClass());
	// if (secondary) EquipWeapon(secondary); else EquipBestWeapon(MUST_EQUIP);
	EquipBestWeapon(MUST_EQUIP); // Placeholder
}

void CFFBot::EquipKnife( void )
{
	// FF_TODO_WEAPONS: Adapt for FF class-specific melee weapons.
	// CFFWeaponBase *melee = FindMeleeWeaponForClass(GetPlayerClass());
	// if (melee) EquipWeapon(melee); else EquipBestWeapon(MUST_EQUIP);
	EquipBestWeapon(MUST_EQUIP); // Placeholder
}

bool CFFBot::EquipGrenade( bool noSpecialGrenades )
{
	// FF_TODO_WEAPONS: Adapt for FF grenade types (Frag, EMP, Concussion, Nail Grenades etc.)
	// noSpecialGrenades might mean "don't equip EMP/Conc if only Frag is desired" or similar.
	// This needs to iterate available grenades and pick one based on profile/situation.
	// CFFWeaponBase *grenade = FindBestGrenade();
	// if (grenade) { EquipWeapon(grenade); return true; }
	return false;
}

bool CFFBot::IsUsingKnife( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;

	switch(weapon->GetWeaponID())
	{
		case FF_WEAPON_CROWBAR:
		case FF_WEAPON_KNIFE:
		case FF_WEAPON_MEDKIT:
		case FF_WEAPON_SPANNER:
		case FF_WEAPON_UMBRELLA:
			return true;
		default:
			return false;
	}
}

bool CFFBot::IsUsingPistol( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;

	FFWeaponID id = weapon->GetWeaponID();
	PlayerClass_t pClass = GetPlayerClass();

	if ( pClass == CLASS_SCOUT && id == FF_WEAPON_NAILGUN) return true; // Scout pistol is Nailgun
	if ( pClass == CLASS_ENGINEER && id == FF_WEAPON_RAILGUN) return true;
	if ( pClass == CLASS_SPY && id == FF_WEAPON_TRANQUILISER) return true;
	// FF_TODO_WEAPONS: Add other classes' pistols if they have unique IDs (e.g. a generic pistol ID like FF_WEAPON_PISTOL)
	return false;
}

bool CFFBot::IsUsingGrenade( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;
	// FF_TODO_WEAPONS: Check against all FF grenade weapon IDs.
	// switch(weapon->GetWeaponID()) { /* case FF_WEAPON_GREN1: case FF_WEAPON_GREN2: return true; */ }
	return false;
}

bool CFFBot::IsUsingSniperRifle( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;
	FFWeaponID id = weapon->GetWeaponID();
	return (id == FF_WEAPON_SNIPERRIFLE || id == FF_WEAPON_AUTORIFLE);
}

bool CFFBot::IsSniper( void ) const
{
	for ( int i = 0; i < MAX_WEAPONS; i++ )
	{
		CBaseCombatWeapon *pWep = GetWeapon(i);
		if (pWep)
		{
			CFFWeaponBase *ffWep = dynamic_cast<CFFWeaponBase*>(pWep);
			if ( ffWep && (ffWep->GetWeaponID() == FF_WEAPON_SNIPERRIFLE || ffWep->GetWeaponID() == FF_WEAPON_AUTORIFLE) )
				return true;
		}
	}
	return false;
}

bool CFFBot::IsSniping( void ) const
{
	return (GetTask() == TASK_MOVE_TO_SNIPER_SPOT || GetTask() == TASK_SNIPING);
}

bool CFFBot::IsUsingShotgun( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;
	FFWeaponID id = weapon->GetWeaponID();
	return (id == FF_WEAPON_SHOTGUN || id == FF_WEAPON_SUPERSHOTGUN);
}

bool CFFBot::IsUsingMachinegun( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;
	return (weapon->GetWeaponID() == FF_WEAPON_ASSAULTCANNON);
}

bool CFFBot::HasGrenade( void ) const
{
	// FF_TODO_WEAPONS: Iterate inventory for any FF grenade types
	// for ( int i = 0; i < MAX_WEAPONS; ++i ) { CFFWeaponBase *w = dynamic_cast<CFFWeaponBase*>(GetWeapon(i)); if (w && IsFFGrenade(w->GetWeaponID())) return true; }
	return false;
}

bool CFFBot::CanActiveWeaponFire( void ) const
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return false;
	return (weapon->GetNextPrimaryAttack() <= gpGlobals->curtime);
}

void CFFBot::GiveWeapon( const char *weaponAlias )
{
	if (weaponAlias == NULL || *weaponAlias == '\0') return;
	FFWeaponID weaponID = AliasToWeaponID( weaponAlias );
	if (weaponID != FF_WEAPON_NONE)
	{
		GiveNamedItem( WeaponIDToAlias(weaponID) );
	}
	else
	{
		Msg( "CFFBot::GiveWeapon - Unknown weapon alias: %s\n", weaponAlias );
	}
}

bool CFFBot::AdjustZoom( float range )
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return true;

	if (!IsUsingSniperRifle()) return true;

	if (m_zoomTimer.IsElapsed())
	{
		ZoomType desiredZoom = NO_ZOOM;
		// FF_TODO_WEAPONS: Ranges might need tuning for FF.
		if (range < 250.0f) desiredZoom = NO_ZOOM; // Example
		else if (range < 1500.0f) desiredZoom = LOW_ZOOM; // Example
		else desiredZoom = HIGH_ZOOM;

		if (GetZoomLevel() != desiredZoom)
		{
			SecondaryAttack(); // Assumes IN_ATTACK2 handles zoom for FF sniper rifles
			m_zoomTimer.Start( 0.3f ); // Zoom cycle time
			return false; // Zooming takes time
		}
	}
	return true; // Already at desired zoom or not zooming
}

CFFBot::ZoomType CFFBot::GetZoomLevel( void )
{
	CFFWeaponBase *gun = GetActiveFFWeapon();
	if (gun == NULL || !IsUsingSniperRifle()) return NO_ZOOM;

	// Player FOV is used to determine zoom level for sniper rifles
	if (m_iFOV == GetDefaultFOV()) return NO_ZOOM;
	// FF_TODO_WEAPONS: These FOV values are examples. Need actual FF sniper rifle zoom FOVs.
	if (m_iFOV == 40) return LOW_ZOOM;
	if (m_iFOV == 10) return HIGH_ZOOM;

	return NO_ZOOM; // Default if FOV doesn't match known zoom levels
}

bool CFFBot::IsPrimaryWeaponEmpty( void ) const
{
	// FF_TODO_WEAPONS: Needs to check actual primary weapon for current class.
	// CFFWeaponBase *primary = FindPrimaryWeaponForClass(GetPlayerClass());
	// if (!primary) return true; // No primary equipped/available
	// return !primary->HasAnyAmmo();
	return false; // Placeholder
}

bool CFFBot::IsPistolEmpty( void ) const
{
	// FF_TODO_WEAPONS: Needs to check actual secondary weapon for current class.
	// CFFWeaponBase *secondary = FindSecondaryWeaponForClass(GetPlayerClass());
	// if (!secondary) return true; // No secondary
	// return !secondary->HasAnyAmmo();
	return false; // Placeholder
}

void CFFBot::ReloadCheck( void )
{
	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon == NULL) return;

	// Using HasPrimaryAmmo() as a general check if it's a reloadable gun.
	// CBaseCombatWeapon defines Clip1() and HasPrimaryAmmo().
	if (weapon->Clip1() <= 0 && weapon->HasPrimaryAmmo())
	{
		Reload(); // Calls IN_RELOAD
	}
}

bool CFFBot::BumpWeapon( CFFWeaponBase *pWeapon ) // FF_WEAPONS: Changed param to CFFWeaponBase
{
	if ( !pWeapon ) return false;

	// FF_TODO_WEAPONS: This logic is mostly CS-specific (buying, defer_to_human_items related to specific CS economy).
	// FF item pickup rules might be different. Bots might only get weapons via class spawn or profile.
	// For now, mostly comment out CS-derived logic.

	if (cv_bot_defer_to_human_items.GetBool() && UTIL_HumansInGame(true) > 0)
	{
		for( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CFFPlayer *player = ToFFPlayer( UTIL_PlayerByIndex( i ) );
			if (player == NULL || player == this || player->IsBot() || player->IsHLTV()) continue;
			if (player->GetTeamNumber() == GetTeamNumber()) return false; // Don't pick up if human teammate nearby
		}
	}

	// FF_TODO_WEAPONS: Check if bot already has this weapon type and if ammo is full.
	// This requires a robust way to identify weapon types/equivalency in FF.
	// if (HasWeapon( pWeapon->GetClassname() ) ) // GetClassname might not be enough for FF variants
	// {
	//		if (pWeapon->HasPrimaryAmmo()) // Check if it's a weapon that uses ammo
	//		{
	//			int ammoType = pWeapon->GetPrimaryAmmoType();
	//			if (GetAmmoCount(ammoType) >= GetMaxCarryAmmo(ammoType)) // GetMaxCarryAmmo from CBasePlayer
	//				return false; // Full of this ammo type
	//		}
	// }

	return BaseClass::BumpWeapon( pWeapon ); // Calls CBasePlayer::BumpWeapon
}

float CFFBot::GetRangeToEnemy( void ) const
{
	if (GetBotEnemy() == NULL)
		return FLT_MAX;
	return (GetBotEnemy()->GetAbsOrigin() - GetAbsOrigin()).Length();
}

float CFFBot::GetCombatRange( void ) const
{
	// FF_TODO_WEAPONS: Adapt for FF weapons and bot skill.
	// This should use weapon properties (eg from CFFWeaponInfo via GetFFWpnData())
	// and bot profile skill.
	float range = 1500.0f; // Default engagement range

	CFFWeaponBase *weapon = GetActiveFFWeapon();
	if (weapon)
	{
		const CFFWeaponInfo &info = weapon->GetFFWpnData();
		if (info.m_flRange > 0) // Assuming m_flRange is effective range from script
		{
			range = info.m_flRange;
		}
		else // Fallback based on weapon type if script range isn't set
		{
			switch(weapon->GetWeaponID())
			{
				case FF_WEAPON_SHOTGUN:
				case FF_WEAPON_SUPERSHOTGUN:
				case FF_WEAPON_FLAMETHROWER: // Short range
				case FF_WEAPON_IC: // Incendiary Cannon, short-mid
					range = 800.0f;
					break;
				case FF_WEAPON_NAILGUN:
				case FF_WEAPON_SUPERNAILGUN:
					range = 1200.0f;
					break;
				case FF_WEAPON_SNIPERRIFLE:
				case FF_WEAPON_AUTORIFLE: // Can be long range
					range = 4000.0f;
					break;
				case FF_WEAPON_RPG:
				case FF_WEAPON_GRENADELAUNCHER:
				case FF_WEAPON_PIPELAUNCHER:
					range = 2500.0f; // Mid-long range explosives
					break;
				case FF_WEAPON_ASSAULTCANNON:
					range = 3000.0f;
					break;
				// Melee weapons would have very short range, handled by attack logic probably
				default:
					range = 1500.0f; // Default for other weapons
					break;
			}
		}
	}

	if ( GetProfile() )
	{
		// Skill level (0 to 1.0). Higher skill = better at judging/using range effectively.
		// This could mean skilled bots engage slightly further or closer depending on weapon.
		// Simple example: scale max range by skill.
		range *= (0.75f + (GetProfile()->GetSkill() * 0.5f));
	}

	return range;
}

//--------------------------------------------------------------------------------------------------------------
// Lua Objective Data Accessors
//--------------------------------------------------------------------------------------------------------------

int CFFBot::GetLuaObjectivePointCount() const
{
	if (TheFFBots())
	{
		return TheFFBots()->GetLuaObjectivePointCount();
	}
	return 0;
}

const CFFBotManager::LuaObjectivePoint* CFFBot::GetLuaObjectivePoint(int index) const
{
	if (TheFFBots())
	{
		return TheFFBots()->GetLuaObjectivePoint(index);
	}
	return NULL;
}

const CUtlVector<CFFBotManager::LuaObjectivePoint>& CFFBot::GetAllLuaObjectivePoints() const
{
	// This is a bit awkward if TheFFBots() is NULL, as we must return a reference.
	// However, TheFFBots() should generally be valid if bots are active.
	// If it can be NULL and this is called, it would likely crash elsewhere anyway.
	// Consider adding an empty static CUtlVector to return in error cases if this becomes an issue.
	static CUtlVector<CFFBotManager::LuaObjectivePoint> s_emptyObjectivePoints;
	if (TheFFBots())
	{
		return TheFFBots()->GetAllLuaObjectivePoints();
	}
	return s_emptyObjectivePoints;
}

const CFFBotManager::LuaObjectivePoint* CFFBot::GetClosestLuaObjectivePoint(const Vector &pos, int teamAffiliation, float maxDist) const
{
	if (!TheFFBots())
	{
		return NULL;
	}

	const CUtlVector<CFFBotManager::LuaObjectivePoint>& objectives = TheFFBots()->GetAllLuaObjectivePoints();
	const CFFBotManager::LuaObjectivePoint* closestPoint = NULL;
	float closestDistSq = (maxDist > 0) ? (maxDist * maxDist) : FLT_MAX;

	for (int i = 0; i < objectives.Count(); ++i)
	{
		const CFFBotManager::LuaObjectivePoint& point = objectives[i];

		// Check team affiliation
		if (teamAffiliation != FF_TEAM_NEUTRAL && // FF_TEAM_NEUTRAL means any team is fine for the objective itself
            point.teamAffiliation != FF_TEAM_NEUTRAL && // Objective is team-specific
            point.teamAffiliation != teamAffiliation)   // Objective team doesn't match requested team
		{
			// Special case: if bot is on a team, and objective is for OTHER team, it might still be relevant (e.g. to attack/defend)
            // This simple getter doesn't make that decision; it just filters by exact affiliation or neutral.
            // More complex logic would be in bot's tactical decision making.
            // For now, if a specific team is requested for the objective, it must match or be neutral.
            if (teamAffiliation != point.teamAffiliation)
                continue;
		}

		float distSq = pos.DistToSqr(point.position);
		if (distSq < closestDistSq)
		{
			closestDistSq = distSq;
			closestPoint = &point;
		}
	}
	return closestPoint;
}

//--------------------------------------------------------------------------------------------------------------
// FF_LUA_OBJECTIVES: Method to set the bot to capture a Lua-defined objective
//--------------------------------------------------------------------------------------------------------------
void CFFBot::CaptureObjective(const CFFBotManager::LuaObjectivePoint* objective)
{
	if (!objective)
	{
		PrintIfWatched("CaptureObjective: Objective pointer is NULL.\n");
		Idle(); // Go idle if no valid objective is provided
		return;
	}

	PrintIfWatched("CaptureObjective: Setting state to capture objective '%s'.\n", objective->name);
	m_captureObjectiveState.SetObjective(objective); // Pass the objective to the state
	SetState(&m_captureObjectiveState);
}

//--------------------------------------------------------------------------------------------------------------
CBaseEntity* CFFBot::FindResourceSource(float maxRange)
{
	CBaseEntity *pBestSource = NULL;
	float flBestDistSq = maxRange * maxRange;

	// Priority 1: Own active dispenser
	CFFDispenser *pOwnDispenser = GetDispenser(); // From CFFPlayer
	if (pOwnDispenser && pOwnDispenser->IsAlive() && pOwnDispenser->IsBuilt() && !pOwnDispenser->IsSapped())
	{
		float distSq = GetAbsOrigin().DistToSqr(pOwnDispenser->GetAbsOrigin());
		if (distSq < flBestDistSq)
		{
			if (ComputePath(pOwnDispenser->GetAbsOrigin(), SAFEST_ROUTE))
			{
				pBestSource = pOwnDispenser;
				flBestDistSq = distSq;
				// If we have our own dispenser and it's close and pathable, it's a very strong candidate.
				// We might even return early, but let's check for closer ammo packs just in case.
			}
		}
	}

	// Priority 2: Other friendly active dispensers
	for (int i = 0; i < IServerTools::GetIServerTools()->MaxEntities(); ++i)
	{
		CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( i ) );
		if (!pEntity || pEntity == pOwnDispenser || !pEntity->IsAlive()) // Skip self or already checked own dispenser
			continue;

		if (FClassnameIs(pEntity, "obj_dispenser"))
		{
			CFFDispenser *pDispenser = dynamic_cast<CFFDispenser *>(pEntity);
			if (pDispenser && pDispenser->GetTeamNumber() == GetTeamNumber() && pDispenser->IsBuilt() && !pDispenser->IsSapped())
			{
				float distSq = GetAbsOrigin().DistToSqr(pDispenser->GetAbsOrigin());
				if (distSq < flBestDistSq)
				{
					if (ComputePath(pDispenser->GetAbsOrigin(), SAFEST_ROUTE))
					{
						pBestSource = pDispenser;
						flBestDistSq = distSq;
					}
				}
			}
		}
	}

	// FF_TODO_RESOURCES: Search for ammo packs like "item_ammo_cells"
	// This requires knowing the classname of cell packs if they exist as separate entities.
	// Example:
	/*
	for (int i = 0; i < IServerTools::GetIServerTools()->MaxEntities(); ++i)
	{
		CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( i ) );
		if (!pEntity || pEntity->IsMarkedForDeletion()) // Ammo packs might not have IsAlive()
			continue;

		if (FClassnameIs(pEntity, "item_ammo_cells")) // Hypothetical classname
		{
			// CItem *pItem = dynamic_cast<CItem *>(pEntity); // If it derives from CItem
			// if (pItem && !pItem->HasBeenPickedUp()) // Conceptual
			// {
				float distSq = GetAbsOrigin().DistToSqr(pEntity->GetAbsOrigin());
				if (distSq < flBestDistSq)
				{
					if (ComputePath(pEntity->GetAbsOrigin(), SAFEST_ROUTE))
					{
						pBestSource = pEntity;
						flBestDistSq = distSq;
					}
				}
			// }
		}
	}
	*/

	if (pBestSource)
	{
		PrintIfWatched("FindResourceSource: Found resource %s at dist %f\n", pBestSource->GetClassname(), sqrt(flBestDistSq));
	}
	return pBestSource;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToFindResources()
{
	if (!IsEngineer())
		return;

	if (GetAmmoCount(AMMO_CELLS) < ENGINEER_LOW_CELL_THRESHOLD) // Using static const from CFFBot.h
	{
		PrintIfWatched("Bot %s (Engineer) is low on cells (%d), trying to find resources.\n", GetPlayerName(), GetAmmoCount(AMMO_CELLS));
		SetState(&m_findResourcesState);
	}
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsSpy(void) const
{
	if (GetPlayerClass())
	{
		return FStrEq(GetFFClassData().m_szClassName, "spy");
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsBehind(const CBaseEntity* target) const
{
	// FF_TODO_SPY: Implement actual geometric check.
	// This would involve comparing the target's forward vector with the vector from target to bot.
	if (!target) return false;

	Vector toTarget = target->GetAbsOrigin() - GetAbsOrigin();
	toTarget.z = 0; // Compare in 2D for simplicity, or use full 3D check
	toTarget.NormalizeInPlace();

	Vector targetForward;
	AngleVectors(target->GetAbsAngles(), &targetForward);
	targetForward.z = 0; // Compare in 2D
	targetForward.NormalizeInPlace();

	// If the dot product is close to -1, the bot is behind the target.
    // Threshold can be adjusted, e.g. > 0 means in front, < 0 means behind.
    // For a stricter "directly behind", dot product should be close to 1 if 'toTarget' was from bot to target.
    // Since toTarget is target to bot, we want dot(toTarget, targetForward) to be positive and large.
    // Let's re-evaluate: vector from bot to target
    Vector botToTarget = GetAbsOrigin() - target->GetAbsOrigin(); // Vector from target to bot
    botToTarget.z = 0;
    botToTarget.NormalizeInPlace();
    // If bot is behind target, botToTarget should be largely opposite to targetForward.
    // So, DotProduct(botToTarget, targetForward) should be close to -1.
	return (DotProduct(botToTarget, targetForward) < -0.75f); // Example threshold: more than 135 degrees apart
}


//--------------------------------------------------------------------------------------------------------------
CBaseEntity* CFFBot::FindSpyTarget(float maxRange)
{
	CBaseEntity *pBestTarget = NULL;
	float flBestTargetScore = 0.0f; // Higher is better
	float maxRangeSq = maxRange * maxRange;

	// Priority 1: Enemy Sentries
	for (int i = 0; i < IServerTools::GetIServerTools()->MaxEntities(); ++i)
	{
		CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( i ) );
		if (!pEntity || !pEntity->IsAlive()) continue;

		if (FClassnameIs(pEntity, "obj_sentrygun"))
		{
			CFFSentryGun *sentry = dynamic_cast<CFFSentryGun *>(pEntity);
			if (sentry && IsEnemy(sentry) && sentry->IsBuilt() && !sentry->IsSapped())
			{
				float distSq = GetAbsOrigin().DistToSqr(sentry->GetAbsOrigin());
				if (distSq < maxRangeSq)
				{
					if (ComputePath(sentry->GetAbsOrigin(), SAFEST_ROUTE))
					{
						float score = 1000.0f - sqrt(distSq); // Simple score: closer is much better
						if (score > flBestTargetScore)
						{
							pBestTarget = sentry;
							flBestTargetScore = score;
						}
					}
				}
			}
		}
	}
	if (pBestTarget) return pBestTarget; // Found a sentry, prioritize it.

	// Priority 2: Enemy Dispensers
	for (int i = 0; i < IServerTools::GetIServerTools()->MaxEntities(); ++i)
	{
		CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( i ) );
		if (!pEntity || !pEntity->IsAlive()) continue;
		if (FClassnameIs(pEntity, "obj_dispenser"))
		{
			CFFDispenser *dispenser = dynamic_cast<CFFDispenser *>(pEntity);
			if (dispenser && IsEnemy(dispenser) && dispenser->IsBuilt() && !dispenser->IsSapped())
			{
				float distSq = GetAbsOrigin().DistToSqr(dispenser->GetAbsOrigin());
				if (distSq < maxRangeSq)
				{
					if (ComputePath(dispenser->GetAbsOrigin(), SAFEST_ROUTE))
					{
						float score = 800.0f - sqrt(distSq);
						if (score > flBestTargetScore)
						{
							pBestTarget = dispenser;
							flBestTargetScore = score;
						}
					}
				}
			}
		}
	}
	if (pBestTarget) return pBestTarget; // Found a dispenser.

	// Priority 3: Enemy Engineers
	CPlayerPickupPlayerEnumerator playerEnum;
	playerpickup_IteratePlayers(&playerEnum, this, TEAM_ANY, false); // Iterate all players, exclude self, include dead (for now)
	for (int i = 0; i < playerEnum.GetPlayerCount(); ++i)
	{
		CFFPlayer *pPlayer = ToFFPlayer(playerEnum.GetPlayer(i));
		if (pPlayer && pPlayer->IsAlive() && IsEnemy(pPlayer))
		{
			if (FStrEq(pPlayer->GetFFClassData().m_szClassName, "engineer")) // Check if Engineer
			{
				float distSq = GetAbsOrigin().DistToSqr(pPlayer->GetAbsOrigin());
				if (distSq < maxRangeSq)
				{
					if (ComputePath(pPlayer->GetAbsOrigin(), SAFEST_ROUTE))
					{
						float score = 600.0f - sqrt(distSq);
						if (score > flBestTargetScore)
						{
							pBestTarget = pPlayer;
							flBestTargetScore = score;
						}
					}
				}
			}
		}
	}
    if (pBestTarget) return pBestTarget; // Found an engineer

	// Priority 4: Other Enemy Players (e.g., high-value targets like Medics, Snipers, or just general players)
	for (int i = 0; i < playerEnum.GetPlayerCount(); ++i) // Reuse playerEnum
	{
		CFFPlayer *pPlayer = ToFFPlayer(playerEnum.GetPlayer(i));
		if (pPlayer && pPlayer->IsAlive() && IsEnemy(pPlayer))
		{
			float distSq = GetAbsOrigin().DistToSqr(pPlayer->GetAbsOrigin());
			if (distSq < maxRangeSq)
			{
				if (ComputePath(pPlayer->GetAbsOrigin(), SAFEST_ROUTE))
				{
					float score = 200.0f - sqrt(distSq);
					// FF_TODO_SPY: Add more scoring based on player class, situation, etc.
					if (FStrEq(pPlayer->GetFFClassData().m_szClassName, "medic")) score += 100;
					if (FStrEq(pPlayer->GetFFClassData().m_szClassName, "sniper")) score += 80;

					if (score > flBestTargetScore)
					{
						pBestTarget = pPlayer;
						flBestTargetScore = score;
					}
				}
			}
		}
	}

	if (pBestTarget) PrintIfWatched("FindSpyTarget: Selected target %s (score %f).\n", pBestTarget->IsPlayer() ? ToFFPlayer(pBestTarget)->GetPlayerName() : pBestTarget->GetClassname(), flBestTargetScore);
	else PrintIfWatched("FindSpyTarget: No suitable target found.\n");

	return pBestTarget;
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToInfiltrate()
{
	if (!IsSpy())
		return;

	// Don't enter infiltrate if already doing something critical or already infiltrating
	if (GetState() == &m_infiltrateState || IsBusy() ) // IsBusy could be expanded for Spy
		return;

	PrintIfWatched("Bot %s (Spy) is trying to infiltrate.\n", GetPlayerName());
	SetState(&m_infiltrateState);
}


//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsMedic(void) const
{
	if (GetPlayerClass()) // GetPlayerClass() is from CBasePlayer, returns CPlayerClassInfo which is CFFPlayerClassInfo for FF
	{
		// GetFFClassData() provides const CFFPlayerClassInfo&
		// m_szClassName is the field storing the script name like "medic", "soldier"
		// The actual class name string "medic" might vary based on game scripts.
		// Using "medic" as a placeholder based on common convention.
		return FStrEq(GetFFClassData().m_szClassName, "medic");
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
CFFPlayer* CFFBot::FindNearbyInjuredTeammate(float maxRange, float healthRatioThreshold)
{
	CFFPlayer *pBestTarget = NULL;
	float flBestTargetHealthRatio = healthRatioThreshold;
	float flBestTargetDistSq = maxRange * maxRange;

	CPlayerPickupPlayerEnumerator playerEnum; // Helper class to iterate players
	playerpickup_IteratePlayers( &playerEnum, this, GetTeamNumber() ); // Iterates living teammates, excluding self

	for ( int i = 0; i < playerEnum.GetPlayerCount(); ++i )
	{
		CBasePlayer *pPlayer = playerEnum.GetPlayer(i);
		// Basic checks already done by IteratePlayers, but double check for safety
		if ( !pPlayer || pPlayer == this || !pPlayer->IsAlive() || pPlayer->GetTeamNumber() != GetTeamNumber() )
			continue;

		CFFPlayer *pFFPlayer = ToFFPlayer(pPlayer);
		if (!pFFPlayer)
			continue;

		float currentHealthRatio = (float)pFFPlayer->GetHealth() / (float)pFFPlayer->GetMaxHealth();

		// Needs healing if below the threshold (e.g. 0.98 means less than 98% health)
		if ( currentHealthRatio < healthRatioThreshold )
		{
			float distSq = GetAbsOrigin().DistToSqr(pFFPlayer->GetAbsOrigin());
			if (distSq < flBestTargetDistSq)
			{
				// Prioritization:
				// 1. Significantly more injured (e.g. 20% lower health ratio)
				// 2. Comparably injured but closer
				// 3. First potential target
				bool bPreferThisTarget = false;
				if (!pBestTarget) // First one
				{
					bPreferThisTarget = true;
				}
				else if (currentHealthRatio < (flBestTargetHealthRatio - 0.2f) ) // Significantly more injured
				{
					bPreferThisTarget = true;
				}
				// Check if comparably injured AND closer (not just closer to any less injured)
				else if (currentHealthRatio < flBestTargetHealthRatio && distSq < flBestTargetDistSq )
				{
					bPreferThisTarget = true;
				}


				if (bPreferThisTarget)
				{
					// Basic visibility or path check - can be more complex
					// For Medics, pathing is more important than direct LoS if they need to move to heal.
					if (ComputePath(pFFPlayer->GetAbsOrigin(), SAFEST_ROUTE) ) // Check if pathable
					{
						pBestTarget = pFFPlayer;
						flBestTargetHealthRatio = currentHealthRatio;
						flBestTargetDistSq = distSq;
					}
					// Fallback to visible if not pathable (maybe stuck behind something temporarily or very close)
					// This helps if pathing fails for very close targets due to nav mesh quirks.
					else if (IsVisible(pFFPlayer) && distSq < Square(150.0f)) // Only if very close and visible for pathing fail
					{
						pBestTarget = pFFPlayer;
						flBestTargetHealthRatio = currentHealthRatio;
						flBestTargetDistSq = distSq;
					}
				}
			}
		}
	}
	return pBestTarget;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::StartHealing(CFFPlayer* target)
{
	if (!IsMedic())
	{
		Warning("Bot %s is not a Medic, cannot StartHealing!\n", GetPlayerName());
		return;
	}

	if (target && target->IsAlive() && target->GetTeamNumber() == GetTeamNumber() && target != this)
	{
		PrintIfWatched( "Bot %s (Medic) is starting to heal %s\n", GetPlayerName(), target->GetPlayerName());
		m_healTeammateState.RequestHealTarget(target); // Pass target to the state
		SetState(&m_healTeammateState);
	}
	else
	{
		 PrintIfWatched( "Bot %s (Medic): Invalid target for StartHealing.\n", GetPlayerName());
	}
}

//--------------------------------------------------------------------------------------------------------------
// Overload for SetTask to handle LuaObjectivePoint if needed by states (like CaptureObjectiveState)
void CFFBot::SetTask( TaskType task, const CFFBotManager::LuaObjectivePoint *objectiveTarget )
{
	// This version of SetTask is primarily for tasks that directly relate to a LuaObjectivePoint
	// which doesn't have a CBaseEntity representation.
	// The base SetTask(TaskType, CBaseEntity*) will handle clearing m_taskEntity if objectiveTarget is NULL.

	if ( task == DEFEND_LUA_OBJECTIVE || task == CAPTURE_LUA_OBJECTIVE )
	{
		// For these tasks, the 'entity' is conceptual, represented by the LuaObjectivePoint.
		// We don't set m_taskEntity here, as it's not a CBaseEntity.
		// The state itself (CaptureObjectiveState) holds the LuaObjectivePoint pointer.
		// We might want to store the name or position if generic task logging needs it,
		// but m_taskEntity should remain NULL or be handled carefully.
		m_task = task;
		m_taskEntity = NULL; // Ensure it's null for non-entity tasks or handle appropriately.
		// Potentially log objectiveTarget->name or position if needed for debugging GetTaskName()
		if (objectiveTarget)
		{
			// For debugging or if GetTaskName needs to be more descriptive for Lua objectives
			// You might store objectiveTarget->name or objectiveTarget->position in a new member variable
			// if you want GetTaskName() or other generic task functions to describe these.
			// For now, just setting the task type.
		}
	}
	else
	{
		// If other tasks were to use this overload, ensure they are handled correctly.
		// For now, defer to the entity-based SetTask for other types if called inadvertently.
		Warning("CFFBot::SetTask called with LuaObjectivePoint for an unhandled task type: %d\n", task);
		SetTask(task, (CBaseEntity *)NULL); // Fallback, clears entity.
	}
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::IsEngineer(void) const
{
	if (GetPlayerClass())
	{
		// Assuming "engineer" is the script name for the Engineer class
		return FStrEq(GetFFClassData().m_szClassName, "engineer");
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::HasSentry(void) const
{
	// A CFFPlayer has methods like GetSentryGun() which returns a CFFSentryGun*
	// This check assumes CFFBot inherits from CFFPlayer or has a similar way to access player's buildables.
	// CFFBot is a CBot<CFFPlayer>, so 'this' is a CFFPlayer.
	if ( GetSentryGun() != NULL )
		 return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToBuildSentry(const Vector *location)
{
	if (!IsEngineer())
	{
		Warning("Bot %s is not an Engineer, cannot TryToBuildSentry!\n", GetPlayerName());
		return;
	}

	if (HasSentry())
	{
		PrintIfWatched("Bot %s (Engineer) already has a sentry. Not building another.\n", GetPlayerName());
		return;
	}

	// FF_TODO_ENGINEER: Add checks for resources (cells/metal) if applicable in FF.
	// This might involve checking GetAmmoCount(AMMO_CELLS) against a cost.
	if (GetAmmoCount(AMMO_CELLS) < SENTRY_COST_CELLS)
	{
		PrintIfWatched("Bot %s (Engineer) doesn't have enough cells (%d required) to build a sentry.\n", GetPlayerName(), SENTRY_COST_CELLS);
		// me->SetTask(CFFBot::TaskType::SEEK_AND_DESTROY); // Or some other task like collect resources
		return;
	}

	PrintIfWatched("Bot %s (Engineer) is trying to build a sentry.\n", GetPlayerName());
	if (location)
	{
		m_buildSentryState.SetBuildLocation(*location);
	}
	else
	{
		// If no location is provided, state will pick a default or use more complex logic.
		m_buildSentryState.SetBuildLocation(vec3_invalid);
	}
	SetState(&m_buildSentryState);
}

//--------------------------------------------------------------------------------------------------------------
bool CFFBot::HasDispenser(void) const
{
	// Assumes CFFPlayer (which CFFBot is a CBot<CFFPlayer>) has GetDispenser()
	if ( GetDispenser() != NULL )
		 return true;
	return false;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToBuildDispenser(const Vector *location)
{
	if (!IsEngineer())
	{
		// This check might be redundant if callers already verify IsEngineer()
		// Warning("Bot %s is not an Engineer, cannot TryToBuildDispenser!\n", GetPlayerName());
		return;
	}

	if (HasDispenser())
	{
		PrintIfWatched("Bot %s (Engineer) already has a dispenser. Not building another.\n", GetPlayerName());
		return;
	}

	if (GetAmmoCount(AMMO_CELLS) < DISPENSER_COST_CELLS)
	{
		PrintIfWatched("Bot %s (Engineer) doesn't have enough cells (%d required) to build a dispenser.\n", GetPlayerName(), DISPENSER_COST_CELLS);
		return;
	}

	PrintIfWatched("Bot %s (Engineer) is trying to build a dispenser.\n", GetPlayerName());
	if (location)
	{
		m_buildDispenserState.SetBuildLocation(*location);
	}
	else
	{
		m_buildDispenserState.SetBuildLocation(vec3_invalid);
	}
	SetState(&m_buildDispenserState);
}

//--------------------------------------------------------------------------------------------------------------
CBaseEntity* CFFBot::FindNearbyDamagedFriendlyBuildable(float maxRange)
{
	CBaseEntity *pBestTarget = NULL;
	float flBestTargetDistSq = maxRange * maxRange;
	CBaseEntity *pBestSappedTarget = NULL;
	float flBestSappedTargetDistSq = maxRange * maxRange;


	// Iterate all entities to find buildables
	for (int i = 0; i < IServerTools::GetIServerTools()->MaxEntities(); ++i)
	{
		CBaseEntity *pEntity = CBaseEntity::Instance( engine->PEntityOfEntIndex( i ) );

		if (pEntity == NULL || pEntity->IsMarkedForDeletion() || !pEntity->IsAlive())
			continue;

		// Check if it's a buildable type we care about (obj_sentrygun, obj_dispenser)
		// FF_TODO_BUILDING: Add other repairable buildables if any (e.g., teleporters)
		if (FClassnameIs(pEntity, "obj_sentrygun") || FClassnameIs(pEntity, "obj_dispenser"))
		{
			CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(pEntity);
			if (!pBuildable)
				continue;

			// Must be on our team
			if (pBuildable->GetTeamNumber() != GetTeamNumber())
				continue;

			float distSq = GetAbsOrigin().DistToSqr(pBuildable->GetAbsOrigin());
			if (distSq >= flBestTargetDistSq && distSq >= flBestSappedTargetDistSq) // Optimization: if further than both current bests
				continue;

			// Check for pathability first, as it's a hard requirement
			if (!ComputePath(pBuildable->GetAbsOrigin(), SAFEST_ROUTE))
				continue;

			// Prioritize Sapped Buildings
			if (pBuildable->IsSapped()) // Assumes IsSapped() method exists on CFFBuildableObject
			{
				if (distSq < flBestSappedTargetDistSq)
				{
					pBestSappedTarget = pBuildable;
					flBestSappedTargetDistSq = distSq;
				}
			}
			// If not sapped, consider for normal repair if damaged
			else if (pBuildable->GetHealth() < pBuildable->GetMaxHealth())
			{
				if (distSq < flBestTargetDistSq)
				{
					pBestTarget = pBuildable;
					flBestTargetDistSq = distSq;
				}
			}
		}
	}

	// Return sapped target first if one was found
	if (pBestSappedTarget)
	{
		PrintIfWatched("FindNearbyDamagedFriendlyBuildable: Prioritizing sapped buildable %s\n", pBestSappedTarget->GetClassname());
		return pBestSappedTarget;
	}

	if (pBestTarget)
	{
		PrintIfWatched("FindNearbyDamagedFriendlyBuildable: Found damaged buildable %s\n", pBestTarget->GetClassname());
	}
	return pBestTarget;
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::TryToRepairBuildable(CBaseEntity* targetBuildable)
{
	if (!IsEngineer())
		return;

	CBaseEntity *buildableToRepair = targetBuildable;

	if (!buildableToRepair) // If no specific target was given, try to find one
	{
		buildableToRepair = FindNearbyDamagedFriendlyBuildable();
	}

	if (buildableToRepair)
	{
		CFFBuildableObject *pBuildable = dynamic_cast<CFFBuildableObject *>(buildableToRepair);
		if (pBuildable && pBuildable->IsAlive() && pBuildable->GetHealth() < pBuildable->GetMaxHealth())
		{
			PrintIfWatched("Bot %s (Engineer) is starting to repair %s\n", GetPlayerName(), pBuildable->GetClassname());
			m_repairBuildableState.SetTargetBuildable(pBuildable);
			SetState(&m_repairBuildableState);
		}
		else if (pBuildable)
		{
			PrintIfWatched("Bot %s (Engineer): Target buildable %s is not damaged or invalid.\n", GetPlayerName(), pBuildable->GetClassname());
		}
	}
	else
	{
		PrintIfWatched("Bot %s (Engineer): No damaged friendly buildables found to repair.\n", GetPlayerName());
	}
}


//--------------------------------------------------------------------------------------------------------------
int CFFBot::GetLuaPathPointCount() const
{
	if (TheFFBots())
	{
		return TheFFBots()->GetLuaPathPointCount();
	}
	return 0;
}

const CFFBotManager::LuaPathPoint* CFFBot::GetLuaPathPoint(int index) const
{
	if (TheFFBots())
	{
		return TheFFBots()->GetLuaPathPoint(index);
	}
	return NULL;
}

const CUtlVector<CFFBotManager::LuaPathPoint>& CFFBot::GetAllLuaPathPoints() const
{
	static CUtlVector<CFFBotManager::LuaPathPoint> s_emptyPathPoints;
	if (TheFFBots())
	{
		return TheFFBots()->GetAllLuaPathPoints();
	}
	return s_emptyPathPoints;
}

const CFFBotManager::LuaPathPoint* CFFBot::GetClosestLuaPathPoint(const Vector &pos, float maxDist) const
{
	if (!TheFFBots())
	{
		return NULL;
	}

	const CUtlVector<CFFBotManager::LuaPathPoint>& pathPoints = TheFFBots()->GetAllLuaPathPoints();
	const CFFBotManager::LuaPathPoint* closestPoint = NULL;
	float closestDistSq = (maxDist > 0) ? (maxDist * maxDist) : FLT_MAX;

	for (int i = 0; i < pathPoints.Count(); ++i)
	{
		const CFFBotManager::LuaPathPoint& point = pathPoints[i];
		float distSq = pos.DistToSqr(point.position);
		if (distSq < closestDistSq)
		{
			closestDistSq = distSq;
			closestPoint = &point;
		}
	}
	return closestPoint;
}
