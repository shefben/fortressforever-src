//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h" // FF_CHANGE: Changed from cs_bot.h
#include "ff_weapon_base.h" // FF_WEAPONS: Added
// #include "basecsgrenade_projectile.h" // FF_TODO_WEAPONS: CS specific, likely not needed directly

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Fire our active weapon towards our current enemy
 * NOTE: Aiming our weapon is handled in RunBotUpkeep() -> CFFBot::AimAtEnemy() -> CFFBot::UpdateAimOffset()
 */
void CFFBot::FireWeaponAtEnemy( void )
{
	if (cv_bot_dont_shoot.GetBool())
	{
		return;
	}

	CFFPlayer *enemy = GetBotEnemy(); // FF_CHANGE: Changed from CBasePlayer
	if (enemy == NULL)
	{
		return;
	}

	// Vector myOrigin = GetCentroid( this ); // Unused

	if (IsUsingSniperRifle()) // This now calls CFFBot::IsUsingSniperRifle
	{
		// if we're using a sniper rifle, don't fire until we are standing still, are zoomed in, and not rapidly moving our view
		if (!IsNotMoving() || IsWaitingForZoom() || !HasViewBeenSteady( GetProfile()->GetReactionTime() ) )
		{
			return;
		}
	}

	if (gpGlobals->curtime > m_fireWeaponTimestamp &&
		GetTimeSinceAcquiredCurrentEnemy() >= GetProfile()->GetAttackDelay() &&
		!IsSurprised())
	{
		// FF_TODO_WEAPONS: Shield logic removed
		// if (!(IsRecognizedEnemyProtectedByShield() && IsPlayerFacingMe( enemy )) &&
		if (!IsReloading() &&
			!IsActiveWeaponClipEmpty() &&
			IsEnemyVisible())
		{
			// we have a clear shot - pull trigger if we are aiming at enemy
			Vector toAimSpot = m_aimSpot - EyePosition();
			float rangeToEnemy = toAimSpot.NormalizeInPlace();

			if ( IsUsingSniperRifle() ) // This now calls CFFBot::IsUsingSniperRifle
			{
				// FF_TODO_WEAPONS: CS-specific spread values and AWP check. Adapt for FF sniper rifles.
				// CFFWeaponBase *weapon = GetActiveFFWeapon();
				// if (weapon)
				// {
				//		float fProjectedSpread = rangeToEnemy * weapon->GetInaccuracy(); // GetInaccuracy might need to be on CFFWeaponBase
				//		float fRequiredSpread = (weapon->GetWeaponID() == FF_WEAPON_SNIPERRIFLE_HEAVY_EXAMPLE) ? 50.0f : 25.0f;
				//		if ( fProjectedSpread > fRequiredSpread )
				//			return;
				// }
			}

			Vector aimDir = GetViewVector();
			float onTarget = DotProduct( toAimSpot, aimDir );

			const float halfSize = (IsUsingSniperRifle()) ? HalfHumanWidth : 2.0f * HalfHumanWidth; // FF_TODO_WEAPONS: Re-evaluate for FF
			float aimTolerance = (float)cos( atan( halfSize / rangeToEnemy ) );

			if (onTarget > aimTolerance)
			{
				bool doAttack = true;

				if (TheFFBots()->AllowFriendlyFireDamage()) // FF_CHANGE: TheCSBots -> TheFFBots
				{
					if (IsFriendInLineOfFire()) // This is a CBot method, should be fine
						doAttack = false;
				}

				if (doAttack)
				{
					if (IsUsingKnife()) // This now calls CFFBot::IsUsingKnife
					{
						const float knifeRange = 75.0f;
						if (rangeToEnemy < knifeRange)
						{
							ForceRun( 5.0f );
							if (!IsPlayerFacingMe( enemy )) // IsPlayerFacingMe takes CFFPlayer
							{
								SecondaryAttack();
							}
							else
							{
								const float knifeStabChance = 33.3f;
								if (RandomFloat( 0, 100 ) < knifeStabChance)
									SecondaryAttack();
								else
									PrimaryAttack();
							}
						}
					}
					else
					{
						PrimaryAttack();
					}
				}

				// FF_TODO_WEAPONS: Firing rate logic based on weapon type needs FF adaptation
				if (IsUsingPistol()) // This now calls CFFBot::IsUsingPistol
				{
					const float closePistolRange = 360.0f;
					if (GetProfile()->GetSkill() > 0.75f && rangeToEnemy < closePistolRange)
					{
						m_fireWeaponTimestamp = 0.0f;
					}
					else
					{
						m_fireWeaponTimestamp = RandomFloat( 0.15f, 0.4f );
					}
				}
				else
				{
					const float sprayRange = 400.0f;
					if (GetProfile()->GetSkill() < 0.5f || rangeToEnemy < sprayRange || IsUsingMachinegun()) // This now calls CFFBot::IsUsingMachinegun
					{
						m_fireWeaponTimestamp = 0.0f;
					}
					else
					{
						const float distantTargetRange = 800.0f;
						if (!IsUsingSniperRifle() && rangeToEnemy > distantTargetRange)
						{
							m_fireWeaponTimestamp = RandomFloat( 0.3f, 0.7f );
						}
						else
						{
							m_fireWeaponTimestamp = RandomFloat( 0.15f, 0.25f );
						}
					}
				}
				m_fireWeaponTimestamp -= g_BotUpdateInterval;
				m_fireWeaponTimestamp += gpGlobals->curtime;
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Set the current aim offset using given accuracy (1.0 = perfect aim, 0.0f = terrible aim)
 */
void CFFBot::SetAimOffset( float accuracy ) // FF_CHANGE: CCSBot to CFFBot
{
	if (accuracy < 1.0f)
	{
		if (IsViewMoving( 100.0f ))
			m_aimSpreadTimestamp = gpGlobals->curtime;

		const float focusTime = MAX( 5.0f * (1.0f - accuracy), 2.0f );
		float focusInterval = gpGlobals->curtime - m_aimSpreadTimestamp;
		float focusAccuracy = focusInterval / focusTime;
		const float maxFocusAccuracy = 0.75f;
		if (focusAccuracy > maxFocusAccuracy)
			focusAccuracy = maxFocusAccuracy;
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
void CFFBot::UpdateAimOffset( void ) // FF_CHANGE: CCSBot to CFFBot
{
	if (gpGlobals->curtime >= m_aimOffsetTimestamp)
	{
		SetAimOffset( GetProfile()->GetSkill() );
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
 * Return true if the zoom level changed. (Method moved to CFFBot::AdjustZoom in ff_bot.cpp)
 */
// bool CFFBot::AdjustZoom( float range ) - Implementation now in ff_bot.cpp

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using the specific weapon
 */
// bool CFFBot::IsUsing( FFWeaponID weaponID ) const // FF_CHANGE & FF_WEAPONS
// {
// 	CFFWeaponBase *weapon = GetActiveFFWeapon(); // FF_WEAPONS
// 	if (weapon == NULL)
// 		return false;
// 	return (weapon->GetWeaponID() == weaponID); // GetWeaponID() is on CFFWeaponBase
// }

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we are using a weapon with a removable silencer
 * (Method CFFBot::DoesActiveWeaponHaveSilencer now in ff_bot.h and commented out)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a sniper rifle
 * (Method CFFBot::IsUsingSniperRifle now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a sniper rifle in our inventory
 * (Method CFFBot::IsSniper now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are actively sniping (moving to sniper spot or settled in)
 * (Method CFFBot::IsSniping now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we are using a shotgun
 * (Method CFFBot::IsUsingShotgun now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if using the big 'ol machinegun
 * (Method CFFBot::IsUsingMachinegun now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if primary weapon doesn't exist or is totally out of ammo
 * (Method CFFBot::IsPrimaryWeaponEmpty now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if pistol doesn't exist or is totally out of ammo
 * (Method CFFBot::IsPistolEmpty now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the given item
 */
bool CFFBot::DoEquip( CFFWeaponBase *weapon ) // FF_CHANGE & FF_WEAPONS
{
	if (weapon == NULL)
		return false;

	// check if weapon has any ammo left (some weapons might not use ammo, e.g. spanner)
	// CFFWeaponInfo &info = weapon->GetFFWpnData(); // Example if needed
	// if (info.iMaxClip1 > 0 && !weapon->HasAnyAmmo()) // Check if it's a weapon that uses ammo and has none
	//	return false;

	if (weapon->GetMaxClip1() > 0 && !weapon->HasAnyAmmo()) // General check for ammo-based weapons
	    return false;


	// equip it
	SelectItem( weapon->GetClassname() ); // GetClassname is from CBaseNetworkable
	m_equipTimer.Start();

	return true;
}


// throttle how often equipping is allowed
const float minEquipInterval = 5.0f; // This was CS, might need tuning for FF


//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the best weapon we are carrying that has ammo
 * (Method CFFBot::EquipBestWeapon now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip our pistol
 * (Method CFFBot::EquipPistol now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip the knife
 * (Method CFFBot::EquipKnife now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if we have a grenade in our inventory
 * (Method CFFBot::HasGrenade now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Equip a grenade, return false if we cant
 * (Method CFFBot::EquipGrenade now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have knife equipped
 * (Method CFFBot::IsUsingKnife now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have pistol equipped
 * (Method CFFBot::IsUsingPistol now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if we have a grenade equipped
 * (Method CFFBot::IsUsingGrenade now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Begin the process of throwing the grenade
 */
void CFFBot::ThrowGrenade( const Vector &target ) // FF_CHANGE: CCSBot to CFFBot
{
	// FF_TODO_WEAPONS: This method needs to be adapted for FF grenade mechanics.
	// IsUsingGrenade() and other checks should be FF specific.
	if (IsUsingGrenade() && m_grenadeTossState == NOT_THROWING && !IsOnLadder())
	{
		m_grenadeTossState = START_THROW;
		m_tossGrenadeTimer.Start( 2.0f ); // Grenade prime/throw time

		const float angleTolerance = 3.0f;
		SetLookAt( "GrenadeThrow", target, PRIORITY_UNINTERRUPTABLE, 4.0f, false, angleTolerance );

		Wait( RandomFloat( 2.0f, 4.0f ) ); // Wait time before actual throw

		if (cv_bot_debug.GetBool() && IsLocalPlayerWatchingMe())
		{
			NDebugOverlay::Cross3D( target, 25.0f, 255, 125, 0, true, 3.0f );
		}
		PrintIfWatched( "%3.2f: Grenade: START_THROW\n", gpGlobals->curtime );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if our weapon can attack
 * (Method CFFBot::CanActiveWeaponFire now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Find spot to throw grenade ahead of us and "around the corner" along our path
 */
bool CFFBot::FindGrenadeTossPathTarget( Vector *pos ) // FF_CHANGE: CCSBot to CFFBot
{
	// This logic is fairly generic geometry/navmesh query, may work for FF with minimal changes.
	if (!HasPath())
		return false;

	int i;
	for( i=m_pathIndex; i<m_pathLength; ++i )
	{
		if (!FVisible( m_path[i].pos + Vector( 0, 0, HalfHumanHeight ) ))
			break;
	}

	if (i == m_pathIndex)
		return false;

	Vector dir = m_path[i].pos - m_path[i-1].pos;
	float length = dir.NormalizeInPlace();

	const float inc = 25.0f;
	Vector p;
	Vector visibleSpot = m_path[i-1].pos;
	for( float t = 0.0f; t<length; t += inc )
	{
		p = m_path[i-1].pos + t * dir;
		p.z += HalfHumanHeight;
		if (!FVisible( p ))
			break;
		visibleSpot = p;
	}
	visibleSpot.z += 10.0f;
	const float bufferRange = 50.0f;
	trace_t result;
	Vector check;

	check = visibleSpot + Vector( 999.9f, 0, 0 );
	UTIL_TraceLine( visibleSpot, check, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );
	if (result.fraction < 1.0f) { if (result.endpos.x - visibleSpot.x < bufferRange) visibleSpot.x = result.endpos.x - bufferRange; }

	check = visibleSpot + Vector( -999.9f, 0, 0 );
	UTIL_TraceLine( visibleSpot, check, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );
	if (result.fraction < 1.0f) { if (visibleSpot.x - result.endpos.x < bufferRange) visibleSpot.x = result.endpos.x + bufferRange; }

	check = visibleSpot + Vector( 0, 999.9f, 0 );
	UTIL_TraceLine( visibleSpot, check, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );
	if (result.fraction < 1.0f) { if (result.endpos.y - visibleSpot.y < bufferRange) visibleSpot.y = result.endpos.y - bufferRange; }

	check = visibleSpot + Vector( 0, -999.9f, 0 );
	UTIL_TraceLine( visibleSpot, check, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );
	if (result.fraction < 1.0f) { if (visibleSpot.y - result.endpos.y < bufferRange) visibleSpot.y = result.endpos.y + bufferRange; }

	*pos = visibleSpot;
	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Look for grenade throw targets and throw the grenade
 */
void CFFBot::LookForGrenadeTargets( void ) // FF_CHANGE: CCSBot to CFFBot
{
	// FF_TODO_WEAPONS: Adapt for FF grenade types and encounter logic.
	if (!IsUsingGrenade() || IsThrowingGrenade())
	{
		return;
	}

	const CNavArea *tossArea = GetInitialEncounterArea();
	if (tossArea == NULL)
	{
		return;
	}

	int enemyTeam = OtherTeam( GetTeamNumber() ); // OtherTeam is now FF specific (from ff_bot_manager.h)

	if (tossArea->GetEarliestOccupyTime( enemyTeam ) > gpGlobals->curtime)
	{
		EquipBestWeapon( MUST_EQUIP );
		return;
	}

	Vector tossTarget = Vector( 0, 0, 0 );
	if (!tossArea->IsVisible( EyePosition(), &tossTarget ))
	{
		return;
	}

	CFFWeaponBase *weapon = GetActiveFFWeapon(); // FF_WEAPONS
	// FF_TODO_WEAPONS: CS-specific WEAPON_SMOKEGRENADE check. Adapt if FF has smoke grenades with different tactical use.
	// if (weapon && weapon->GetWeaponID() == FF_WEAPON_SMOKEGRENADE_EXAMPLE)
	// {
	//	ThrowGrenade( tossTarget );
	//	PrintIfWatched( "Throwing smoke grenade!" );
	//	SetInitialEncounterArea( NULL );
	//	return;
	// }
	// else	// other grenade types
	// {
		const float leadTime = 1.5f;
		float enemyTime = tossArea->GetEarliestOccupyTime( enemyTeam );
		if (enemyTime - TheFFBots()->GetElapsedRoundTime() > leadTime) // FF_CHANGE
		{
			return;
		}

		Vector to = tossTarget - EyePosition();
		float range = to.Length();
		const float slope = 0.2f;
		float tossHeight = slope * range;
		trace_t result;
		CTraceFilterNoNPCsOrPlayer traceFilter( this, COLLISION_GROUP_NONE );
		const float heightInc = tossHeight / 10.0f;
		Vector target;
		float safeSpace = tossHeight / 2.0f;
		const Vector& eyePosition = EyePosition();
		Vector mins = VEC_HULL_MIN; Vector maxs = VEC_HULL_MAX;
		mins.z = 0; maxs.z = heightInc;

		float low = 0.0f; float high = tossHeight + safeSpace;
		bool gotLow = false; float lastH = 0.0f;
		for( float h = 0.0f; h < 3.0f * tossHeight; h += heightInc )
		{
			target = tossTarget + Vector( 0, 0, h );
			Ray_t ray; ray.Init( eyePosition, target, mins, maxs );
			enginetrace->TraceRay( ray, MASK_VISIBLE_AND_NPCS | CONTENTS_GRATE, &traceFilter, &result );
			if (result.fraction == 1.0f)
			{
				if (!gotLow) { low = h; gotLow = true; }
			}
			else
			{
				if (gotLow) { high = lastH; break; }
			}
			lastH = h;
		}

		if (gotLow)
		{
			if (tossHeight < low) tossHeight = (low + safeSpace > high) ? (high + low)/2.0f : low + safeSpace;
			else if (tossHeight > high - safeSpace) tossHeight = (high - safeSpace < low) ? (high + low)/2.0f : high - safeSpace;
			ThrowGrenade( tossTarget + Vector( 0, 0, tossHeight ) );
			SetInitialEncounterArea( NULL );
			return;
		}
	// }
}


//--------------------------------------------------------------------------------------------------------------
class FOVClearOfFriends // FF_CHANGE: Changed CCSBot to CFFBot where m_me is used
{
public:
	FOVClearOfFriends( CFFBot *me )
	{
		m_me = me;
	}

	bool operator() ( CBasePlayer *player )
	{
		if (player == m_me || !player->IsAlive())
			return true;

		if (m_me->InSameTeam( player ))
		{
			Vector to = player->EyePosition() - m_me->EyePosition();
			to.NormalizeInPlace();

			Vector forward;
			m_me->EyeVectors( &forward );

			if (DotProduct( to, forward ) > 0.95f)
			{
				// FF_CHANGE: Cast to CFFPlayer for IsVisible
				if (m_me->IsVisible( static_cast<CFFPlayer *>(player) ))
				{
					return false;
				}
			}
		}
		return true;
	}
	CFFBot *m_me;
};

//--------------------------------------------------------------------------------------------------------------
/**
 * Process the grenade throw state machine
 */
void CFFBot::UpdateGrenadeThrow( void ) // FF_CHANGE: CCSBot to CFFBot
{
	switch( m_grenadeTossState )
	{
		case START_THROW:
		{
			if (m_tossGrenadeTimer.IsElapsed())
			{
				EquipBestWeapon( MUST_EQUIP );
				ClearLookAt();
				m_grenadeTossState = NOT_THROWING;
				PrintIfWatched( "%3.2f: Grenade: THROW FAILED\n", gpGlobals->curtime );
				return;
			}
			if (m_lookAtSpotState == LOOK_AT_SPOT)
			{
				FOVClearOfFriends fovClear( this );
				if (ForEachPlayer( fovClear )) // ForEachPlayer iterates CBasePlayer, which is fine for FOVClearOfFriends
				{
					m_grenadeTossState = FINISH_THROW;
					m_tossGrenadeTimer.Start( 1.0f );
					PrintIfWatched( "%3.2f: Grenade: FINISH_THROW\n", gpGlobals->curtime );
				}
				else
				{
					PrintIfWatched( "%3.2f: Grenade: Friend is in the way...\n", gpGlobals->curtime );
				}
			}
			PrimaryAttack();
			break;
		}
		case FINISH_THROW:
		{
			if (m_tossGrenadeTimer.IsElapsed())
			{
				ClearLookAt();
				m_grenadeTossState = NOT_THROWING;
				PrintIfWatched( "%3.2f: Grenade: THROW COMPLETE\n", gpGlobals->curtime );
			}
			break;
		}
		default:
		{
			if (IsUsingGrenade())
			{
				PrimaryAttack();
			}
			break;
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
class GrenadeResponse // FF_CHANGE: Changed CCSBot to CFFBot
{
public:
	GrenadeResponse( CFFBot *me ) { m_me = me; }

	bool operator() ( ActiveGrenade *ag ) const
	{
		const float retreatRange = 300.0f;
		const float hideTime = 1.0f;

		if (m_me->IsVisible( ag->GetPosition(), CHECK_FOV, (CBaseEntity *)ag->GetEntity() ))
		{
			// FF_TODO_WEAPONS: Adapt for FF grenade types and effects
			if (ag->IsSmoke()) return true; // Example: Ignore smokes for now

			Vector velDir = ag->GetEntity()->GetAbsVelocity();
			float grenadeSpeed = velDir.NormalizeInPlace();
			const float atRestSpeed = 50.0f;
			const float aboutToBlow = 0.5f;

			// FF_TODO_WEAPONS: Flashbang logic is CS specific. Adapt if FF has flashbangs.
			// if (ag->IsFlashbang() && ag->GetEntity()->m_flDetonateTime - gpGlobals->curtime < aboutToBlow)
			// {
			//		QAngle eyeAngles = m_me->EyeAngles();
			//		float yaw = RandomFloat( 100.0f, 135.0f );
			//		eyeAngles.y += (RandomFloat( -1.0f, 1.0f ) < 0.0f) ? (-yaw) : yaw;
			//		Vector forward; AngleVectors( eyeAngles, &forward );
			//		Vector away = m_me->EyePosition() - 1000.0f * forward;
			//		m_me->ClearLookAt();
			//		m_me->SetLookAt( "Avoid Flashbang", away, PRIORITY_UNINTERRUPTABLE, 2.0f );
			//		m_me->StopAiming();
			//		return false;
			// }

			const float throwDangerRange = 750.0f; const float nearDangerRange = 300.0f;
			Vector to = ag->GetPosition() - m_me->GetAbsOrigin(); float range = to.NormalizeInPlace();
			if (range > throwDangerRange) return true;

			if (grenadeSpeed > atRestSpeed)
			{
				if (DotProduct( to, velDir ) >= -0.5f) return true; // Going away
				m_me->PrintIfWatched( "Retreating from a grenade thrown towards me!\n" );
			}
			else if (range < nearDangerRange)
			{
				m_me->PrintIfWatched( "Retreating from a grenade that landed near me!\n" );
			}
			m_me->TryToRetreat( retreatRange, hideTime );
			return false;
		}
		return true;
	}
	CFFBot *m_me;
};

/**
 * React to enemy grenades we see
 */
void CFFBot::AvoidEnemyGrenades( void ) // FF_CHANGE: CCSBot to CFFBot
{
	if (GetProfile()->GetSkill() < 0.5) return;
	if (IsAvoidingGrenade()) return;

	GrenadeResponse respond( this );
	if (TheFFBots()->ForEachGrenade( respond ) == false) // FF_CHANGE
	{
		const float avoidTime = 4.0f;
		m_isAvoidingGrenade.Start( avoidTime );
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Reload our weapon if we must
 * (Method CFFBot::ReloadCheck now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Silence/unsilence our weapon if we must
 * (Method CFFBot::SilencerCheck now in ff_bot.h and commented out as CS-specific)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when in contact with a CBaseCombatWeapon
 * (Method CFFBot::BumpWeapon now in ff_bot.cpp)
 */

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if a friend is in our weapon's way
 */
bool CFFBot::IsFriendInLineOfFire( void ) // FF_CHANGE: CCSBot to CFFBot
{
	Vector aimDir = GetViewVector();
	trace_t result;
	UTIL_TraceLine( EyePosition(), EyePosition() + 10000.0f * aimDir, MASK_PLAYERSOLID, this, COLLISION_GROUP_NONE, &result );

	if (result.DidHitNonWorldEntity())
	{
		CBaseEntity *victim = result.m_pEnt;
		if (victim && victim->IsPlayer() && victim->IsAlive())
		{
			CFFPlayer *player = static_cast<CFFPlayer *>( victim ); // FF_CHANGE
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
float CFFBot::ComputeWeaponSightRange( void ) // FF_CHANGE: CCSBot to CFFBot
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
bool CFFBot::DidPlayerJustFireWeapon( const CFFPlayer *player ) const // FF_CHANGE
{
	CFFWeaponBase *weapon = player->GetActiveFFWeapon(); // FF_CHANGE & FF_WEAPONS
	// FF_TODO_WEAPONS: IsSilenced() may not exist on CFFWeaponBase or might work differently.
	// return (weapon && !weapon->IsSilenced() && weapon->m_flNextPrimaryAttack > gpGlobals->curtime);
	return (weapon && weapon->GetNextPrimaryAttack() > gpGlobals->curtime); // Simplified for now
}
