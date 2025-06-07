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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

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
