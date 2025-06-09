//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot.h"
#include "ff_bot_manager.h"
#include "../ff_player.h"
#include "../../shared/ff/ff_gamerules.h"
#include "../../shared/ff/weapons/ff_weapon_base.h"
// #include "../../shared/ff/weapons/ff_weapon_parse.h"
#include "ff_gamestate.h"
#include "nav_mesh.h"
#include "bot_constants.h"
#include "KeyValues.h"      // Already included, seems fine

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
/**
 * Checks if the bot can hear the event
 */
void CFFBot::OnAudibleEvent( IGameEvent *event, CBasePlayer *player, float range, PriorityType priority, bool isHostile, bool isFootstep, const Vector *actualOrigin )
{
	/// @todo Listen to non-player sounds
	if (player == NULL)
		return;

	// don't pay attention to noise that friends make
	if (!IsEnemy( player )) // IsEnemy takes CBaseEntity*, player is CBasePlayer* here, should be fine
		return;

	Vector playerOrigin = GetCentroid( player );
	Vector myOrigin = GetCentroid( this );

	// If the event occurs far from the triggering player, it may override the origin
	if ( actualOrigin )
	{
		playerOrigin = *actualOrigin;
	}

	// check if noise is close enough for us to hear
	const Vector *newNoisePosition = &playerOrigin;
	float newNoiseDist = (myOrigin - *newNoisePosition).Length();
	if (newNoiseDist < range)
	{
		// we heard the sound
		if ((IsLocalPlayerWatchingMe() && cv_bot_debug.GetInt() == 3) || cv_bot_debug.GetInt() == 4)
		{
			PrintIfWatched( "Heard noise (%s from %s, pri %s, time %3.1f)\n", 
											(FStrEq( "weapon_fire", event->GetName() )) ? "Weapon fire " : "", // TODO: Update event names for FF if different
											(player) ? player->GetPlayerName() : "NULL",
											(priority == PRIORITY_HIGH) ? "HIGH" : ((priority == PRIORITY_MEDIUM) ? "MEDIUM" : "LOW"),
											gpGlobals->curtime );
		}

		// should we pay attention to it
		// if noise timestamp is zero, there is no prior noise
		if (m_noiseTimestamp > 0.0f)
		{
			// only overwrite recent sound if we are louder (closer), or more important - if old noise was long ago, its faded
			const float shortTermMemoryTime = 3.0f;
			if (gpGlobals->curtime - m_noiseTimestamp < shortTermMemoryTime)
			{
				// prior noise is more important - ignore new one
				if (priority < m_noisePriority)
					return;

				float oldNoiseDist = (myOrigin - m_noisePosition).Length();
				if (newNoiseDist >= oldNoiseDist)
					return;
			}
		}

		// find the area in which the noise occured
		/// @todo Better handle when noise occurs off the nav mesh
		/// @todo Make sure noise area is not through a wall or ceiling from source of noise
		/// @todo Change GetNavTravelTime to better deal with NULL destination areas
		if (!TheNavMesh) return; // Guard against null TheNavMesh
		CNavArea *noiseArea = TheNavMesh->GetNearestNavArea( *newNoisePosition );
		if (noiseArea == NULL)
		{
			PrintIfWatched( "  *** Noise occurred off the nav mesh - ignoring!\n" );
			return;
		}

		m_noiseArea = noiseArea;

		// remember noise priority
		m_noisePriority = priority;

		// randomize noise position in the area a bit - hearing isn't very accurate
		// the closer the noise is, the more accurate our placement
		/// @todo Make sure not to pick a position on the opposite side of ourselves.
		const float maxErrorRadius = 400.0f;
		const float maxHearingRange = 2000.0f;
		float errorRadius = maxErrorRadius * newNoiseDist/maxHearingRange;

		m_noisePosition.x = newNoisePosition->x + RandomFloat( -errorRadius, errorRadius );
		m_noisePosition.y = newNoisePosition->y + RandomFloat( -errorRadius, errorRadius );

		// note the *travel distance* to the noise
		m_noiseTravelDistance = GetTravelDistanceToPlayer( static_cast<CFFPlayer *>(player) ); // Cast CBasePlayer to CFFPlayer

		// make sure noise position remains in the same area
		m_noiseArea->GetClosestPointOnArea( m_noisePosition, &m_noisePosition );

		// note when we heard the noise
		m_noiseTimestamp = gpGlobals->curtime;

		// if we hear a nearby enemy, become alert
		const float nearbyNoiseRange = 1000.0f;
		if (m_noiseTravelDistance < nearbyNoiseRange && m_noiseTravelDistance > 0.0f)
		{
			BecomeAlert();
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnHEGrenadeDetonate( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 99999.0f, PRIORITY_HIGH, true ); // hegrenade_detonate // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnFlashbangDetonate( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1000.0f, PRIORITY_LOW, true ); // flashbang_detonate // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnSmokeGrenadeDetonate( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1000.0f, PRIORITY_LOW, true ); // smokegrenade_detonate // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnGrenadeBounce( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 500.0f, PRIORITY_LOW, true ); // grenade_bounce // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBulletImpact( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// Construct an origin for the sound, since it can be far from the originating player
	Vector actualOrigin;
	actualOrigin.x = event->GetFloat( "x", 0.0f );
	actualOrigin.y = event->GetFloat( "y", 0.0f );
	actualOrigin.z = event->GetFloat( "z", 0.0f );

	/// @todo Ignoring bullet impact events for now - we dont want bots to look directly at them!
	//OnAudibleEvent( event, player, 1100.0f, PRIORITY_MEDIUM, true, false, &actualOrigin ); // bullet_impact
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBreakProp( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_MEDIUM, true ); // break_prop // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnBreakBreakable( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_MEDIUM, true ); // break_glass // TODO: Update event name for FF (likely break_breakable)
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnDoorMoving( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_MEDIUM, false ); // door_moving // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
// TODO: Hostage logic is CS-specific and needs to be adapted or removed for FF.
void CFFBot::OnHostageFollows( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	// player_follows needs a player
	if (player == NULL)
		return;

	// don't pay attention to noise that friends make
	if (!IsEnemy( player ))
		return;

	Vector playerOrigin = GetCentroid( player );
	Vector myOrigin = GetCentroid( this );
	const float range = 1200.0f;

	// TODO_FF: Hostage logic is CS-specific. The following block will need review for FF.
	// For now, class name changes are the priority. Chatter system is removed.
	// Team enums (TEAM_TERRORIST), GameState methods (GetNearestVisibleFreeHostage),
	// and BotTasks (GUARD_HOSTAGE_RESCUE_ZONE, SEEK_AND_DESTROY) are CS-specific.
	// if (GetTeamNumber() == TEAM_TERRORIST_FF) // Example FF team
	// {
	// 	// make sure we can hear the noise
	// 	if ((playerOrigin - myOrigin).IsLengthGreaterThan( range ))
	// 		return;
	// 	// GetChatter()->HostagesBeingTaken(); // Chatter system removed
	// 	// if (GetGameState()->GetNearestVisibleFreeObjectiveItem() == NULL) // Example FF objective
	// 	// {
	// 	// 	if (GetTask() != BOT_TASK_GUARD_OBJECTIVE_ZONE_FF) // Example FF task
	// 	// 	{
	// 	// 		// ... logic ...
	// 	// 	}
	// 	// }
	// }
	// else
	// {
	// 	// CT's don't care about this noise
	// 	return;
	// }

	OnAudibleEvent( event, player, range, PRIORITY_MEDIUM, false ); // hostage_follows // TODO_FF: Update event name for FF if different
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnRoundEnd( IGameEvent *event )
{
	// Morale adjustments happen even for dead players
	// TODO_FF: Update WINNER_TER, WINNER_CT for FF teams/win conditions (e.g. TEAM_RED_FF, TEAM_BLUE_FF using game rules)
	int winnerTeamID = event->GetInt( "winner" );

	// Example FF logic (assumes FFGameRules() and proper team ID definitions):
	// CFFGameRules *pRules = FFGameRules();
	// if (pRules) {
	// 	if (pRules->IsTeamValid(winnerTeamID)) {
	// 		if (GetTeamNumber() == winnerTeamID) {
	// 			IncreaseMorale();
	// 		} else if (pRules->IsTeamValid(GetTeamNumber())) { // Ensure bot is on a valid team before decreasing morale
	// 			DecreaseMorale();
	// 		}
	// 	}
	// } else
	{ // Fallback to CS-style logic if FFGameRules() is not available or doesn't handle it
		switch ( winnerTeamID )
		{
		case WINNER_TER: // CS Specific
			if (GetTeamNumber() == TEAM_CT) // CS Specific team check
			{
				DecreaseMorale();
			}
			else if (GetTeamNumber() == TEAM_TERRORIST) // CS Specific team check
			{
				IncreaseMorale();
			}
			break;

		case WINNER_CT: // CS Specific
			if (GetTeamNumber() == TEAM_CT) // CS Specific team check
			{
				IncreaseMorale();
			}
			else if (GetTeamNumber() == TEAM_TERRORIST) // CS Specific team check
			{
				DecreaseMorale();
			}
			break;

		default:
			break;
		}
	}


	m_gameState.OnRoundEnd( event );

	if ( !IsAlive() )
		return;

	// TODO_FF: Update WINNER_TER, WINNER_CT for FF teams/win conditions. Chatter system is removed.
	// Example FF logic:
	// CFFGameRules *pRules = FFGameRules();
	// if (pRules && pRules->IsTeamValid(winnerTeamID) && GetTeamNumber() == winnerTeamID)
	// {
	// 	// GetChatter()->CelebrateWin(); // Chatter system removed
	// }
	// Fallback CS-style logic:
	if ( event->GetInt( "winner" ) == WINNER_TER ) // CS Specific
	{
		// if (GetTeamNumber() == TEAM_TERRORIST_FF) GetChatter()->CelebrateWin(); // Chatter system removed
	}
	else if ( event->GetInt( "winner" ) == WINNER_CT ) // CS Specific
	{
		// if (GetTeamNumber() == TEAM_CT_FF) GetChatter()->CelebrateWin(); // Chatter system removed
	}
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnRoundStart( IGameEvent *event )
{
	m_gameState.OnRoundStart( event );
}


//--------------------------------------------------------------------------------------------------------------
// TODO_FF: Hostage logic is CS-specific and needs to be adapted or removed for FF.
void CFFBot::OnHostageRescuedAll( IGameEvent *event )
{
	// m_gameState.OnObjectiveCompletedAll( event ); // Example for FF, if applicable
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnNavBlocked( IGameEvent *event )
{
	if ( event->GetBool( "blocked" ) )
	{
		unsigned int areaID = event->GetInt( "area" );
		if ( areaID )
		{
			// An area was blocked off.  Reset our path if it has this area on it.
			for( int i=0; i<m_pathLength; ++i )
			{
				const ConnectInfo *info = &m_path[ i ]; // ConnectInfo is defined in CFFBot
				if ( info->area && info->area->GetID() == areaID )
				{
					DestroyPath();
					return;
				}
			}
		}
	}
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Invoked when bot enters a nav area
 */
void CFFBot::OnEnteredNavArea( CNavArea *newArea )
{
	if (!newArea) return; // Added null check

	// assume that we "clear" an area of enemies when we enter it
	newArea->SetClearedTimestamp( GetTeamNumber()-1 ); // TODO: Ensure GetTeamNumber() is 0-indexed or 1-indexed as appropriate for FF

	// if we just entered a 'stop' area, set the flag
	if ( newArea->GetAttributes() & NAV_MESH_STOP )
	{
		m_isStopping = true;
	}

	/// @todo Flag these areas as spawn areas during load
	if (IsAtEnemySpawn())
	{
		m_hasVisitedEnemySpawn = true;
	}
}
