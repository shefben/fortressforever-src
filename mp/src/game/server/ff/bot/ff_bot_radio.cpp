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
#include "../../shared/ff/weapons/ff_weapon_base.h" // For CFFWeaponBase (potentially used via CFFBot)
// #include "../../shared/ff/weapons/ff_weapon_parse.h" // For CFFWeaponInfo (potentially used)
#include "../../shared/ff/ff_gamerules.h" // For FFGameRules() (potentially used)
#include "ff_gamestate.h"   // For FFGameState
#include "nav_mesh.h"       // For TheNavMesh, CNavArea, Place, UNDEFINED_PLACE
#include "nav_pathfind.h"   // For PathCost, NavAreaTravelDistance
#include "bot_constants.h"  // For RadioType, RADIO_INVALID, etc.
#include "usermessages.h"   // For UserMessageBegin, WRITE_BYTE, etc. (for RawAudio)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// extern int gmsgBotVoice; // This is an old global message ID, not typically used in Source. Replaced by usermessages.

//--------------------------------------------------------------------------------------------------------------
/**
 * Returns true if the radio message is an order to do something
 * NOTE: "Report in" is not considered a "command" because it doesnt ask the bot to go somewhere, or change its mind
 */
// TODO: Update RadioType enums for FF
bool CFFBot::IsRadioCommand( RadioType event ) const
{
	if (event == RADIO_AFFIRMATIVE ||
		event == RADIO_NEGATIVE ||
		event == RADIO_ENEMY_SPOTTED ||
		event == RADIO_SECTOR_CLEAR ||
		event == RADIO_REPORTING_IN ||
		event == RADIO_REPORT_IN_TEAM ||
		event == RADIO_ENEMY_DOWN ||
		event == RADIO_INVALID )
		return false;

	return true;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Respond to radio commands from HUMAN players
 */
void CFFBot::RespondToRadioCommands( void )
{
	// bots use the chatter system to respond to each other
	if (m_radioSubject != NULL && m_radioSubject->IsPlayer())
	{
		CFFPlayer *player = static_cast<CFFPlayer*>(m_radioSubject.Get()); // Use Get() for EHANDLE
		if (player && player->IsBot()) // Null check for player
		{
			m_lastRadioCommand = RADIO_INVALID;
			return;
		}
	}
	
	if (m_lastRadioCommand == RADIO_INVALID)
		return;

	// a human player has issued a radio command
	GetChatter()->ResetRadioSilenceDuration();


	// if we are doing something important, ignore the radio
	// unless it is a "report in" request - we can do that while we continue to do other things
	/// @todo Create "uninterruptable" flag
	// TODO: Update RadioType enums for FF
	if (m_lastRadioCommand != RADIO_REPORT_IN_TEAM)
	{
		if (IsBusy())
		{
			// consume command
			m_lastRadioCommand = RADIO_INVALID;
			return;
		}
	}

	// wait for reaction time before responding
	// delay needs to be long enough for the radio message we're responding to to finish
	float respondTime = 1.0f + 2.0f * GetProfile()->GetReactionTime();
	if (IsRogue())
		respondTime += 2.0f;

	if (gpGlobals->curtime - m_lastRadioRecievedTimestamp < respondTime)
		return;

	// rogues won't follow commands, unless already following the player
	if (!IsFollowing() && IsRogue())
	{
		if (IsRadioCommand( m_lastRadioCommand ))
		{
			GetChatter()->Negative();
		}

		// consume command
		m_lastRadioCommand = RADIO_INVALID;
		return;
	}

	CFFPlayer *player = static_cast<CFFPlayer*>(m_radioSubject.Get()); // Use Get() for EHANDLE
	if (player == NULL)
	{
		m_lastRadioCommand = RADIO_INVALID; // Consume command if subject is no longer valid
		return;
	}


	// respond to command
	bool canDo = false;
	const float inhibitAutoFollowDuration = 60.0f;
	// TODO: Update RadioType enums and associated logic for FF
	switch( m_lastRadioCommand )
	{
		case RADIO_REPORT_IN_TEAM:
		{
			GetChatter()->ReportingIn();
			break;
		}

		case RADIO_FOLLOW_ME:
		case RADIO_COVER_ME:
		case RADIO_STICK_TOGETHER_TEAM:
		case RADIO_REGROUP_TEAM:
		{
			if (!IsFollowing())
			{
				Follow( player );
				// player->AllowAutoFollow(); // This method might not exist on CFFPlayer directly
				canDo = true;
			}
			break;
		}

		case RADIO_ENEMY_SPOTTED:
		case RADIO_NEED_BACKUP:
		case RADIO_TAKING_FIRE:
			if (!IsFollowing())
			{
				Follow( player );
				GetChatter()->Say( "OnMyWay" ); // TODO: Ensure "OnMyWay" chatter exists
				// player->AllowAutoFollow();
				canDo = false; // This was false, implies it's not an affirmative action but a response to an alert
			}
			break;

		case RADIO_TEAM_FALL_BACK:
		{
			if (TryToRetreat())
				canDo = true;
			break;
		}

		case RADIO_HOLD_THIS_POSITION:
		{
			// find the leader's area 
			SetTask( CFFBot::HOLD_POSITION ); // TODO: Ensure TaskType enums are correct
			StopFollowing();
			// player->InhibitAutoFollow( inhibitAutoFollowDuration ); // This method might not exist on CFFPlayer
			if (TheNavMesh) Hide( TheNavMesh->GetNearestNavArea( m_radioPosition ) ); // Null check TheNavMesh
			canDo = true;
			break;
		}

		case RADIO_GO_GO_GO:
		case RADIO_STORM_THE_FRONT:
			StopFollowing();
			Hunt();
			canDo = true;
			// player->InhibitAutoFollow( inhibitAutoFollowDuration );
			break;

		case RADIO_GET_OUT_OF_THERE:
			// TODO: Update for FF bomb logic
			if (TheFFBots() && TheFFBots()->IsBombPlanted()) // Null check TheFFBots
			{
				EscapeFromBomb();
				// player->InhibitAutoFollow( inhibitAutoFollowDuration );
				canDo = true;
			}
			break;

		case RADIO_SECTOR_CLEAR:
		{
			// TODO: Update for FF scenarios and teams
			// if this is a defusal scenario, and the bomb is planted, 
			// and a human player cleared a bombsite, check it off our list too
			if (TheFFBots() && TheFFBots()->GetScenario() == CFFBotManager::SCENARIO_DEFUSE_BOMB) // Null check, SCENARIO_DEFUSE_BOMB is CS
			{
				if (GetTeamNumber() == TEAM_CT && TheFFBots()->IsBombPlanted()) // TEAM_CT is CS
				{
					const CFFBotManager::Zone *zone = TheFFBots()->GetClosestZone( player ); // Null check TheFFBots
					if (zone)
					{
						GetGameState()->ClearBombsite( zone->m_index );

						// if we are huting for the planted bomb, re-select bombsite
						if (GetTask() == CFFBot::FIND_TICKING_BOMB) // FIND_TICKING_BOMB is CS
							Idle();

						canDo = true;
					}
				}
			}			
			break;
		}

		default:
			// ignore all other radio commands for now
			m_lastRadioCommand = RADIO_INVALID; // Consume if not handled
			return;
	}

	if (canDo)
	{
		// affirmative
		GetChatter()->Affirmative();

		// if we agreed to follow a new command, put away our grenade
		if (IsRadioCommand( m_lastRadioCommand ) && IsUsingGrenade())
		{
			EquipBestWeapon();
		}
	}

	// consume command
	m_lastRadioCommand = RADIO_INVALID;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Decide if we should move to help the player, return true if we will
 */
bool CFFBot::RespondToHelpRequest( CFFPlayer *them, Place place, float maxRange )
{
	if (!them) return false; // Null check

	if (IsRogue())
		return false;

	// if we're busy, ignore
	if (IsBusy())
		return false;

	Vector themOrigin = GetCentroid( them );

	// if we are too far away, ignore
	if (maxRange > 0.0f)
	{
		// compute actual travel distance
		PathCost cost(this); // PathCost needs to be defined
		if (!m_lastKnownArea || !TheNavMesh) return false; // Null checks
		CNavArea *themArea = TheNavMesh->GetNearestNavArea( themOrigin );
		if (!themArea) return false;

		float travelDistance = NavAreaTravelDistance( m_lastKnownArea, themArea, cost ); // NavAreaTravelDistance needs definition
		if (travelDistance < 0.0f)
			return false;

		if (travelDistance > maxRange)
			return false;
	}


	if (place == UNDEFINED_PLACE) // UNDEFINED_PLACE
	{
		// if we have no "place" identifier, go directly to them

		// if we are already there, ignore
		float rangeSq = (them->GetAbsOrigin() - GetAbsOrigin()).LengthSqr();
		const float close = 750.0f * 750.0f;
		if (rangeSq < close)
			return true;

		MoveTo( themOrigin, FASTEST_ROUTE ); // FASTEST_ROUTE
	}
	else
	{
		// if we are already there, ignore
		if (GetPlace() == place)
			return true;

		// go to where help is needed
		const Vector *pos = GetRandomSpotAtPlace( place ); // GetRandomSpotAtPlace needs definition
		if (pos)
		{
			MoveTo( *pos, FASTEST_ROUTE ); // FASTEST_ROUTE
		}
		else
		{
			MoveTo( themOrigin, FASTEST_ROUTE ); // FASTEST_ROUTE
		}
	}

	// acknowledge
	GetChatter()->Say( "OnMyWay" ); // TODO: Ensure "OnMyWay" chatter exists

	return true;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Send a radio message
 */
// TODO: Update RadioType enums and logic for FF
void CFFBot::SendRadioMessage( RadioType event )
{
	// make sure this is a radio event
	if (event <= RADIO_START_1 || event >= RADIO_END) // RADIO_START_1, RADIO_END are CS specific
		return;

	const char *eventName = NameToRadioEvent(event); // NameToRadioEvent needs to be defined
	PrintIfWatched( "%3.1f: SendRadioMessage( %s )\n", gpGlobals->curtime, eventName ? eventName : "INVALID" );


	// note the time the message was sent
	if (TheFFBots()) TheFFBots()->SetRadioMessageTimestamp( event, GetTeamNumber() ); // Null check, ensure GetTeamNumber is 0/1 for FF

	m_lastRadioSentTimestamp = gpGlobals->curtime;

	// TODO: HandleMenu_Radio1/2/3 are CS specific and need FF equivalents or removal.
	// This is a placeholder for how FF might send radio commands.
	// It might involve calling a player command like "radio1", "radio2", "radio3" with the slot.
	// Example: engine->ClientCommand(edict(), "radio%d %d\n", radioGroup, radioSlot);

	// char slot[2];
	// slot[1] = '\000';

	// if (event > RADIO_START_1 && event < RADIO_START_2)
	// {
	//	HandleMenu_Radio1( event - RADIO_START_1 );
	// }
	// else if (event > RADIO_START_2 && event < RADIO_START_3)
	// {
	//	HandleMenu_Radio2( event - RADIO_START_2 );
	// }
	// else
	// {
	//	HandleMenu_Radio3( event - RADIO_START_3 );
	// }
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Send voice chatter.  Also sends the entindex and duration for voice feedback.
 */
void CFFBot::SpeakAudio( const char *voiceFilename, float duration, int pitch )
{
	if( !IsAlive() )
		return;

	if ( IsObserver() ) // IsObserver might need to be CPlayer method or similar
		return;

	CRecipientFilter filter;
	// ConstructRadioFilter( filter ); // ConstructRadioFilter needs to be defined/ported for FF
    filter.AddAllPlayers(); // Example: send to all, or use FF specific team filter

	UserMessageBegin ( filter, "RawAudio" ); // "RawAudio" might need to be registered for FF
		WRITE_BYTE( pitch );
		WRITE_BYTE( entindex() );
		WRITE_FLOAT( duration );
		WRITE_STRING( voiceFilename );
	MessageEnd();

	GetChatter()->ResetRadioSilenceDuration();

	m_voiceEndTimestamp = gpGlobals->curtime + duration;
}
