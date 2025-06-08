//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h" // For TheFFBots()
#include "../ff_player.h"     // For CFFPlayer
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase, FFWeaponID, WEAPONTYPE_*
#include "../../shared/ff/weapons/ff_weapon_parse.h"  // For GetCSWpnData (becomes GetFFWpnData), CFFWeaponInfo
// #include "../../shared/ff/ff_gamerules.h" // For FFGameRules() (potentially used)
#include "ff_gamestate.h"   // For FFGameState
#include "bot_constants.h"  // For BotTaskType, PriorityType, etc.
#include "bot_profile.h"    // For BotProfile

// TODO: Replace "basecsgrenade_projectile.h" with FF equivalent if grenade projectiles are handled this way
// #include "basecsgrenade_projectile.h"


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

	CBasePlayer *enemy = GetBotEnemy();
	if (enemy == NULL)
	{
		return;
	}

	// Vector myOrigin = GetCentroid( this ); // Unused

	// TODO: Update for FF sniper rifle logic
	if (IsUsingSniperRifle())
	{
		// if we're using a sniper rifle, don't fire until we are standing still, are zoomed in, and not rapidly moving our view
		if (!IsNotMoving() || IsWaitingForZoom() || (GetProfile() && !HasViewBeenSteady( GetProfile()->GetReactionTime() )) ) // Null check GetProfile
		{
			return;
		}
	}

	if (gpGlobals->curtime > m_fireWeaponTimestamp &&
		(!GetProfile() || GetTimeSinceAcquiredCurrentEnemy() >= GetProfile()->GetAttackDelay()) && // Null check GetProfile
		!IsSurprised())
	{
		// TODO: Update for FF shield logic if any
		// if (!(IsRecognizedEnemyProtectedByShield() && IsPlayerFacingMe( enemy )) &&
		if (!IsReloading() &&
			!IsActiveWeaponClipEmpty() && 
			IsEnemyVisible())
		{
			// we have a clear shot - pull trigger if we are aiming at enemy
			Vector toAimSpot = m_aimSpot - EyePosition();
			float rangeToEnemy = toAimSpot.NormalizeInPlace();

			// TODO: Update for FF sniper rifle logic (weapon ID, GetInaccuracy, required spread)
			// if ( IsUsingSniperRifle() )
			// {
			//	// check our accuracy versus our target distance
			//	float fProjectedSpread = rangeToEnemy * GetActiveCSWeapon()->GetInaccuracy();
			//	float fRequiredSpread = IsUsing( FF_WEAPON_AWP ) ? 50.0f : 25.0f;	// AWP will kill with any hit // FF_WEAPON_AWP is example
			//	if ( fProjectedSpread > fRequiredSpread )
			//		return;
			// }

			// get actual view direction vector
			Vector aimDir = GetViewVector();

			float onTarget = DotProduct( toAimSpot, aimDir );

			// aim more precisely with a sniper rifle
			// because rifles' bullets spray, don't have to be very precise
			// TODO: Update for FF sniper rifle logic and HalfHumanWidth definition
			const float halfSize = (IsUsingSniperRifle()) ? HalfHumanWidth : 2.0f * HalfHumanWidth;

			// aiming tolerance depends on how close the target is - closer targets subtend larger angles
			float aimTolerance = (rangeToEnemy > FLT_EPSILON) ? (float)cos( atan( halfSize / rangeToEnemy ) ) : 1.0f; // Avoid div by zero

			if (onTarget > aimTolerance)
			{
				bool doAttack;

				// if friendly fire is on, don't fire if a teammate is blocking our line of fire
				if (TheFFBots() && TheFFBots()->AllowFriendlyFireDamage()) // Null check
				{
					if (IsFriendInLineOfFire())
						doAttack = false;
					else
						doAttack = true;
				}
				else
				{
					// fire freely
					doAttack = true;
				}

				if (doAttack)
				{
					// TODO: Update for FF knife logic (IsUsingKnife, SecondaryAttack for backstab)
					// if (IsUsingKnife())
					// {
					//	const float knifeRange = 75.0f;		// 50
					//	if (rangeToEnemy < knifeRange)
					//	{
					//		// since we've given ourselves away - run!
					//		ForceRun( 5.0f );

					//		// if our prey is facing away, backstab him!
					//		if (!IsPlayerFacingMe( enemy ))
					//		{
					//			SecondaryAttack();
					//		}
					//		else
					//		{
					//			// randomly choose primary and secondary attacks with knife
					//			const float knifeStabChance = 33.3f;
					//			if (RandomFloat( 0, 100 ) < knifeStabChance)
					//				SecondaryAttack();
					//			else
					//				PrimaryAttack();
					//		}
					//	}
					// }
					// else
					{
						PrimaryAttack();
					}
				}

				// TODO: Update for FF pistol logic and skill-based firing rates
				// if (IsUsingPistol())
				// {
				//	// high-skill bots fire their pistols quickly at close range
				//	const float closePistolRange = 360.0f;
				//	if (GetProfile() && GetProfile()->GetSkill() > 0.75f && rangeToEnemy < closePistolRange) // Null check
				//	{
				//		// fire as fast as possible
				//		m_fireWeaponTimestamp = 0.0f;
				//	}
				//	else
				//	{
				//		// fire somewhat quickly
				//		m_fireWeaponTimestamp = RandomFloat( 0.15f, 0.4f );
				//	}
				// }
				// else	// not using a pistol
				{
					const float sprayRange = 400.0f;
					// TODO: Update for FF machinegun logic
					if ((GetProfile() && GetProfile()->GetSkill() < 0.5f) || rangeToEnemy < sprayRange || IsUsingMachinegun()) // Null check
					{
						// spray 'n pray if enemy is close, or we're not that good, or we're using the big machinegun
						m_fireWeaponTimestamp = 0.0f;
					}
					else
					{
						const float distantTargetRange = 800.0f;
						// TODO: Update for FF sniper logic
						if (!IsUsingSniperRifle() && rangeToEnemy > distantTargetRange)
						{
							// if very far away, fire slowly for better accuracy
							m_fireWeaponTimestamp = RandomFloat( 0.3f, 0.7f );
						}
						else
						{
							// fire short bursts for accuracy
							m_fireWeaponTimestamp = RandomFloat( 0.15f, 0.25f );		// 0.15, 0.5
						}
					}
				}

				// subtract system latency
				m_fireWeaponTimestamp -= g_BotUpdateInterval; // g_BotUpdateInterval needs to be defined

				m_fireWeaponTimestamp += gpGlobals->curtime;
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
	// if our accuracy is less than perfect, it will improve as we "focus in" while not rotating our view
	if (accuracy < 1.0f)
	{
		// if we moved our view, reset our "focus" mechanism
		if (IsViewMoving( 100.0f ))
			m_aimSpreadTimestamp = gpGlobals->curtime;

		// focusTime is the time it takes for a bot to "focus in" for very good aim, from 2 to 5 seconds
		const float focusTime = MAX( 5.0f * (1.0f - accuracy), 2.0f );
		float focusInterval = gpGlobals->curtime - m_aimSpreadTimestamp;

		float focusAccuracy = (focusTime > FLT_EPSILON) ? (focusInterval / focusTime) : 1.0f; // Avoid div by zero

		// limit how much "focus" will help
		const float maxFocusAccuracy = 0.75f;
		if (focusAccuracy > maxFocusAccuracy)
			focusAccuracy = maxFocusAccuracy;

		accuracy = MAX( accuracy, focusAccuracy );
	}

	// aim error increases with distance, such that actual crosshair error stays about the same
	float range = (m_lastEnemyPosition - EyePosition()).Length();
	float maxOffset = (GetFOV()/GetDefaultFOV()) * 0.05f * range;		// 0.1 // GetDefaultFOV needs to be FF compatible
	float error = maxOffset * (1.0f - accuracy);

	m_aimOffsetGoal.x = RandomFloat( -error, error );
	m_aimOffsetGoal.y = RandomFloat( -error, error );
	m_aimOffsetGoal.z = RandomFloat( -error, error );

	// define time when aim offset will automatically be updated
	m_aimOffsetTimestamp = gpGlobals->curtime + RandomFloat( 0.25f, 1.0f ); // 0.25, 1.5f
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Wiggle aim error based on GetProfile()->GetSkill()
 */
void CFFBot::UpdateAimOffset( void )
{
	if (gpGlobals->curtime >= m_aimOffsetTimestamp)
	{
		if (GetProfile()) SetAimOffset( GetProfile()->GetSkill() ); // Null check
	}

	// move current offset towards goal offset
	Vector d = m_aimOffsetGoal - m_aimOffset;
	const float stiffness = 0.1f; // This could be a convar or profile setting
	m_aimOffset.x += stiffness * d.x;
	m_aimOffset.y += stiffness * d.y;
	m_aimOffset.z += stiffness * d.z;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Change our zoom level to be appropriate for the given range.
 * Return true if the zoom level changed.
 */
// TODO: Update for FF sniper/zoom logic
bool CFFBot::AdjustZoom( float range )
{
	bool adjustZoom = false;

	if (IsUsingSniperRifle())
	{
		const float sniperZoomRange = 150.0f;	// NOTE: This must be less than sniperMinRange in AttackState
		const float sniperFarZoomRange = 1500.0f;

		if (range <= sniperZoomRange)
		{
			if (GetZoomLevel() != NO_ZOOM) adjustZoom = true; // NO_ZOOM from ZoomType enum
		}
		else if (range < sniperFarZoomRange)
		{
			if (GetZoomLevel() != LOW_ZOOM) adjustZoom = true; // LOW_ZOOM from ZoomType enum
		}
		else
		{
			if (GetZoomLevel() != HIGH_ZOOM) adjustZoom = true; // HIGH_ZOOM from ZoomType enum
		}
	}
	else
	{
		if (GetZoomLevel() != NO_ZOOM) adjustZoom = true;
	}

	if (adjustZoom)
	{
		SecondaryAttack();
		m_zoomTimer.Start( 0.25f + (GetProfile() ? (1.0f - GetProfile()->GetSkill()) : 0.5f) ); // Null check
	}

	return adjustZoom;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using the specific weapon
 */
bool CFFBot::IsUsing( FFWeaponID weaponID ) const // Changed CSWeaponID to FFWeaponID
{
	CFFWeaponBase *weapon = GetActiveCSWeapon(); // Should be GetActiveFFWeapon()

	if (weapon == NULL)
		return false;

	// TODO: IsA is CS specific, use GetWeaponID() or similar for FF
	// if (weapon->IsA( weaponID ))
	if (weapon->GetWeaponID() == weaponID)
		return true;

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we are using a weapon with a removable silencer
 */
// TODO: Silencer logic is likely CS specific. Adapt or remove for FF.
bool CFFBot::DoesActiveWeaponHaveSilencer( void ) const
{
	CFFWeaponBase *weapon = GetActiveCSWeapon(); // Should be GetActiveFFWeapon()

	if (weapon == NULL)
		return false;

	// TODO: Replace WEAPON_M4A1 and WEAPON_USP with FF equivalents if they have silencers
	// if (weapon->GetWeaponID() == FF_WEAPON_M4A1 || weapon->GetWeaponID() == FF_WEAPON_USP)
	//	return weapon->IsSilenced(); // Assuming IsSilenced() method exists on CFFWeaponBase

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a sniper rifle
 */
bool CFFBot::IsUsingSniperRifle( void ) const
{
	CFFWeaponBase *weapon = GetActiveCSWeapon(); // Should be GetActiveFFWeapon()

	// TODO: Update for FF weapon types
	if (weapon && weapon->IsKindOf(WEAPONTYPE_SNIPER_RIFLE)) // WEAPONTYPE_SNIPER_RIFLE enum
		return true;

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a sniper rifle in our inventory
 */
bool CFFBot::IsSniper( void ) const
{
	// TODO: Update for FF weapon slots (WEAPON_SLOT_RIFLE) and types
	CFFWeaponBase *weapon = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_PRIMARY ) ); // WEAPON_SLOT_PRIMARY might be more generic

	if (weapon && weapon->IsKindOf(WEAPONTYPE_SNIPER_RIFLE)) // WEAPONTYPE_SNIPER_RIFLE enum
		return true;

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are actively sniping (moving to sniper spot or settled in)
 */
bool CFFBot::IsSniping( void ) const
{
	// TODO: Update TaskType enums for FF
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
	CFFWeaponBase *weapon = GetActiveCSWeapon(); // Should be GetActiveFFWeapon()

	if (weapon == NULL)
		return false;
	// TODO: Update for FF weapon types
	return weapon->IsKindOf(WEAPONTYPE_SHOTGUN); // WEAPONTYPE_SHOTGUN enum
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using the big 'ol machinegun
 */
// TODO: Update for FF machinegun logic (WEAPON_M249)
bool CFFBot::IsUsingMachinegun( void ) const
{
	CFFWeaponBase *weapon = GetActiveCSWeapon(); // Should be GetActiveFFWeapon()

	// if (weapon && weapon->GetWeaponID() == FF_WEAPON_M249) // Example FF enum
	//	return true;

	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if primary weapon doesn't exist or is totally out of ammo
 */
bool CFFBot::IsPrimaryWeaponEmpty( void ) const
{
	// TODO: Update for FF weapon slots (WEAPON_SLOT_RIFLE)
	CFFWeaponBase *weapon = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_PRIMARY ) );

	if (weapon == NULL)
		return true;

	if (weapon->HasAnyAmmo())
		return false;

	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if pistol doesn't exist or is totally out of ammo
 */
bool CFFBot::IsPistolEmpty( void ) const
{
	// TODO: Update for FF weapon slots (WEAPON_SLOT_PISTOL)
	CFFWeaponBase *weapon = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_SECONDARY ) ); // WEAPON_SLOT_SECONDARY might be more generic

	if (weapon == NULL)
		return true;

	if (weapon->HasAnyAmmo())
		return false;

	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the given item
 */
bool CFFBot::DoEquip( CFFWeaponBase *weapon ) // Changed to CFFWeaponBase
{
	if (weapon == NULL)
		return false;

	if (!weapon->HasAnyAmmo())
		return false;

	SelectItem( weapon->GetClassname() ); // SelectItem might take different params or not exist
	m_equipTimer.Start();

	return true;
}


// throttle how often equipping is allowed
const float minEquipInterval = 5.0f;


//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the best weapon we are carrying that has ammo
 */
void CFFBot::EquipBestWeapon( bool mustEquip )
{
	if (!mustEquip && m_equipTimer.GetElapsedTime() < minEquipInterval)
		return;

	if (!TheFFBots()) return; // Null check

	// TODO: Update for FF weapon slots and types
	CFFWeaponBase *primary = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_PRIMARY ) );
	if (primary)
	{
		// This needs FF equivalent of GetCSWpnData() and m_WeaponType
		// CSWeaponType weaponClass = primary->GetFFWpnData().m_WeaponType; // Example
		// if ((TheFFBots()->AllowShotguns() && weaponClass == WEAPONTYPE_SHOTGUN) ||
		//	 // ... other Allow checks for FF weapon types ...
		//	)
		// {
		//	if (DoEquip( primary ))
		//		return;
		// }
	}

	if (TheFFBots()->AllowPistols())
	{
		if (DoEquip( static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_SECONDARY ) ) ))
			return;
	}

	EquipKnife(); // TODO: Ensure EquipKnife works for FF
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip our pistol
 */
void CFFBot::EquipPistol( void )
{
	if (m_equipTimer.GetElapsedTime() < minEquipInterval)
		return;

	if (TheFFBots() && TheFFBots()->AllowPistols() && !IsUsingPistol()) // Null check, TODO: Update IsUsingPistol for FF
	{
		CFFWeaponBase *pistol = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_SECONDARY ) ); // TODO: Update for FF weapon slots
		DoEquip( pistol );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the knife
 */
// TODO: Update for FF knife (weapon name, IsUsingKnife)
void CFFBot::EquipKnife( void )
{
	// if (!IsUsingKnife())
	// {
	//	SelectItem( "weapon_knife" ); // FF knife name
	// }
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a grenade in our inventory
 */
// TODO: Update for FF grenades (slot, types)
bool CFFBot::HasGrenade( void ) const
{
	// CFFWeaponBase *grenade = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_GRENADES ) );
	// return (grenade) ? true : false;
	return false; // Placeholder
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip a grenade, return false if we cant
 */
// TODO: Update for FF grenades (slot, types, IsUsingGrenade, IsSniper)
bool CFFBot::EquipGrenade( bool noSmoke )
{
	// if (IsSniper()) return false;
	// if (IsUsingGrenade()) return true;
	// if (HasGrenade())
	// {
	//	CFFWeaponBase *grenade = static_cast<CFFWeaponBase *>( Weapon_GetSlot( WEAPON_SLOT_GRENADES ) );
	//	if (noSmoke && grenade->GetWeaponID() == FF_WEAPON_SMOKEGRENADE) // Example FF enum
	//		return false;
	//	SelectItem( grenade->GetClassname() );
	//	return true;
	// }
	return false; // Placeholder
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have knife equipped
 */
// TODO: Update for FF knife (GetActiveFFWeapon, GetWeaponID)
bool CFFBot::IsUsingKnife( void ) const
{
	// CFFWeaponBase *weapon = GetActiveFFWeapon();
	// if (weapon && weapon->GetWeaponID() == FF_WEAPON_KNIFE) // Example FF enum
	//	return true;
	return false; // Placeholder
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have pistol equipped
 */
// TODO: Update for FF pistol (GetActiveFFWeapon, IsPistol method)
bool CFFBot::IsUsingPistol( void ) const
{
	// CFFWeaponBase *weapon = GetActiveFFWeapon();
	// if (weapon && weapon->IsPistol()) // IsPistol might need to check WEAPONTYPE_PISTOL
	//	return true;
	return false; // Placeholder
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have a grenade equipped
 */
// TODO: Update for FF grenades (GetActiveFFWeapon, GetWeaponID for various grenade types)
bool CFFBot::IsUsingGrenade( void ) const
{
	// CFFWeaponBase *weapon = GetActiveFFWeapon();
	// if (!weapon) return false;
	// if (weapon->GetWeaponID() == FF_WEAPON_GRENADE_FLASH ||
	//	weapon->GetWeaponID() == FF_WEAPON_GRENADE_SMOKE ||
	//	weapon->GetWeaponID() == FF_WEAPON_GRENADE_HE) // Example FF enums
	//	return true;
	return false; // Placeholder
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin the process of throwing the grenade
 */
// TODO: Update for FF grenades (IsUsingGrenade, SetLookAt, PRIORITY_UNINTERRUPTABLE)
void CFFBot::ThrowGrenade( const Vector &target )
{
	// if (IsUsingGrenade() && m_grenadeTossState == NOT_THROWING && !IsOnLadder()) // NOT_THROWING enum
	// {
	//	m_grenadeTossState = START_THROW; // START_THROW enum
	//	m_tossGrenadeTimer.Start( 2.0f );
	//	const float angleTolerance = 3.0f;
	//	SetLookAt( "GrenadeThrow", target, PRIORITY_UNINTERRUPTABLE, 4.0f, false, angleTolerance );
	//	Wait( RandomFloat( 2.0f, 4.0f ) );
	//	if (cv_bot_debug.GetBool() && IsLocalPlayerWatchingMe())
	//	{
	//		NDebugOverlay::Cross3D( target, 25.0f, 255, 125, 0, true, 3.0f );
	//	}
	//	PrintIfWatched( "%3.2f: Grenade: START_THROW\n", gpGlobals->curtime );
	// }
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if our weapon can attack
 */
bool CFFBot::CanActiveWeaponFire( void ) const
{
	CBaseCombatWeapon *weapon = GetActiveWeapon(); // Use base class method
	return ( weapon && weapon->m_flNextPrimaryAttack <= gpGlobals->curtime );
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Find spot to throw grenade ahead of us and "around the corner" along our path
 */
// TODO: This is complex CS-specific logic, likely needs heavy FF adaptation or removal
bool CFFBot::FindGrenadeTossPathTarget( Vector *pos )
{
	if (!pos || !HasPath()) return false; // Null check
	// ... (original CS logic with many assumptions about visibility and pathing) ...
	return false; // Placeholder
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Look for grenade throw targets and throw the grenade
 */
// TODO: This is complex CS-specific logic, needs heavy FF adaptation or removal
void CFFBot::LookForGrenadeTargets( void )
{
	// if (!IsUsingGrenade() || IsThrowingGrenade()) return;
	// const CNavArea *tossArea = GetInitialEncounterArea();
	// if (tossArea == NULL) return;
	// ... (original CS logic) ...
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Process the grenade throw state machine
 */
// TODO: This is complex CS-specific logic, needs heavy FF adaptation or removal
void CFFBot::UpdateGrenadeThrow( void )
{
	// switch( m_grenadeTossState ) // GrenadeTossState enum
	// {
	// case START_THROW: { ... }
	// case FINISH_THROW: { ... }
	// default: { if (IsUsingGrenade()) PrimaryAttack(); break; }
	// }
}


//--------------------------------------------------------------------------------------------------------------
class GrenadeResponse // This class seems okay generically, but its usage is CS specific
{
public:
	GrenadeResponse( CFFBot *me ) // Changed to CFFBot
	{
		m_me = me;
	}

	bool operator() ( ActiveGrenade *ag ) const // ActiveGrenade needs definition for FF
	{
		if (!m_me || !ag || !ag->GetEntity()) return true; // Null checks

		// TODO: Update for FF grenade types (IsSmoke, IsFlashbang) and timings
		// const float retreatRange = 300.0f;
		// const float hideTime = 1.0f;
		// if (m_me->IsVisible( ag->GetPosition(), CHECK_FOV, (CBaseEntity *)ag->GetEntity() ))
		// {
		// }
		return true;
	}

	CFFBot *m_me;
};

/**
 * React to enemy grenades we see
 */
void CFFBot::AvoidEnemyGrenades( void )
{
	if (!GetProfile() || GetProfile()->GetSkill() < 0.5) return; // Null check
	if (IsAvoidingGrenade()) return;

	// GrenadeResponse respond( this );
	// TODO: TheBots global needs to be TheFFBots, and ForEachGrenade needs FF adaptation
	// if (TheFFBots() && TheFFBots()->ForEachGrenade( respond ) == false)
	// {
	//	const float avoidTime = 4.0f;
	//	m_isAvoidingGrenade.Start( avoidTime );
	// }
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Reload our weapon if we must
 */
void CFFBot::ReloadCheck( void )
{
	const float safeReloadWaitTime = 3.0f;
	const float reloadAmmoRatio = 0.6f;

	if (GetEnemiesRemaining() == 0) return;
	// TODO: Update for FF defusing logic
	// if (IsDefusingBomb() || IsReloading()) return;
	if (IsReloading()) return;


	if (IsActiveWeaponClipEmpty())
	{
		// TODO: Update for FF pistol logic and skill checks
		// if (GetProfile() && GetProfile()->GetSkill() > 0.5f && IsAttacking())
		// {
		//	if (GetActiveCSWeapon() && !GetActiveCSWeapon()->IsPistol() && !IsPistolEmpty())
		//	{
		//		EquipPistol();
		//		return;
		//	}
		// }
	}
	else if (GetTimeSinceLastSawEnemy() > safeReloadWaitTime && GetActiveWeaponAmmoRatio() <= reloadAmmoRatio)
	{
		// if (GetProfile() && GetProfile()->GetSkill() > 0.5f && IsAttacking()) return; // Null check
	}
	else
	{
		return;
	}

	// TODO: Update for FF AWP equivalent and clip logic
	// if (IsUsing( FF_WEAPON_AWP ) && !IsActiveWeaponClipEmpty()) return; // Example FF enum

	Reload();

	if (GetNearbyEnemyCount())
	{
		if (!IsHiding() && GetProfile() && RandomFloat( 0, 100 ) < (25.0f + 100.0f * GetProfile()->GetSkill())) // Null check
		{
			const float safeTime = 5.0f;
			if (GetTimeSinceLastSawEnemy() < safeTime)
			{
				PrintIfWatched( "Retreating to a safe spot to reload!\n" );
				const Vector *spot = FindNearbyRetreatSpot( this, 1000.0f ); // FindNearbyRetreatSpot
				if (spot)
				{
					IgnoreEnemies( 10.0f );
					Run();
					StandUp();
					Hide( *spot, 0.0f );
				}
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Silence/unsilence our weapon if we must
 */
// TODO: Silencer logic is CS specific. Adapt or remove for FF.
void CFFBot::SilencerCheck( void )
{
	// const float safeSilencerWaitTime = 3.5f;
	// if (IsDefusingBomb() || IsReloading() || IsAttacking()) return;
	// if (!DoesActiveWeaponHaveSilencer()) return;
	// if (GetTimeSinceLastSawEnemy() < safeSilencerWaitTime) return;
	// if (GetNearbyEnemyCount() == 0)
	// {
	//	CFFWeaponBase *weapon = GetActiveCSWeapon(); // Should be GetActiveFFWeapon()
	//	if (weapon == NULL || weapon->m_flNextSecondaryAttack >= gpGlobals->curtime) return;
	//	bool isSilencerOn = weapon->IsSilenced();
	//	if (GetProfile() && isSilencerOn != (GetProfile()->PrefersSilencer() || GetProfile()->GetSkill() > 0.7f) /*&& !HasShield()*/) // Shield logic for FF?
	//	{
	//		PrintIfWatched( "%s silencer!\n", (isSilencerOn) ? "Unequipping" : "Equipping" );
	//		weapon->SecondaryAttack();
	//	}
	// }
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when in contact with a CBaseCombatWeapon
 */
// TODO: This logic is highly CS specific (weapon slots, weapon IDs, DropRifle)
bool CFFBot::BumpWeapon( CBaseCombatWeapon *pWeapon )
{
	// CFFWeaponBase *droppedGun = dynamic_cast< CFFWeaponBase* >( pWeapon );
	// if ( droppedGun && droppedGun->GetSlot() == WEAPON_SLOT_PRIMARY ) // WEAPON_SLOT_PRIMARY
	// {
	//	CFFWeaponBase *myGun = dynamic_cast< CFFWeaponBase* >( Weapon_GetSlot( WEAPON_SLOT_PRIMARY ) );
	//	if ( myGun && droppedGun->GetWeaponID() != myGun->GetWeaponID() )
	//	{
	//		if ( GetProfile() && GetProfile()->HasPrimaryPreference() ) // Null check
	//		{
	//			const float safeTime = 2.5f;
	//			if ( GetTimeSinceLastSawEnemy() >= safeTime )
	//			{
	//				for( int i = 0; i < GetProfile()->GetWeaponPreferenceCount(); ++i )
	//				{
	//					FFWeaponID prefID = (FFWeaponID)GetProfile()->GetWeaponPreference( i ); // Cast to FFWeaponID
	//					if (!IsPrimaryWeapon( prefID )) continue; // IsPrimaryWeapon for FF
	//					if ( prefID == myGun->GetWeaponID() ) break;
	//					if ( prefID == droppedGun->GetWeaponID() )
	//					{
	//						// DropRifle(); // FF equivalent
	//						break;
	//					}
	//				}
	//			}
	//		}
	//	}
	// }
	return BaseClass::BumpWeapon( pWeapon ); // Pass original pWeapon
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if a friend is in our weapon's way
 */
bool CFFBot::IsFriendInLineOfFire( void )
{
	Vector aimDir = GetViewVector();
	trace_t result;
	UTIL_TraceLine( EyePosition(), EyePosition() + 10000.0f * aimDir, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );

	if (result.DidHitNonWorldEntity() && result.m_pEnt) // Null check m_pEnt
	{
		CBaseEntity *victim = result.m_pEnt;
		if (victim->IsPlayer() && victim->IsAlive())
		{
			CBasePlayer *player = static_cast<CBasePlayer *>( victim );
			if (player->InSameTeam( this ))
				return true;
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
	UTIL_TraceLine( EyePosition(), EyePosition() + 10000.0f * aimDir, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );
	return (EyePosition() - result.endpos).Length();
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if the given player just fired their weapon
 */
bool CFFBot::DidPlayerJustFireWeapon( const CFFPlayer *player ) const
{
	if (!player) return false; // Null check
	CFFWeaponBase *weapon = static_cast<CFFWeaponBase*>(player->GetActiveWeapon()); // Cast to CFFWeaponBase
	// TODO: IsSilenced() might be CS specific. Does FF have silenced weapons?
	return (weapon && /*!weapon->IsSilenced() &&*/ weapon->m_flNextPrimaryAttack > gpGlobals->curtime);
}
