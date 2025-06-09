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
void CFFBot::OnPlayerDeath( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	Vector playerOrigin = (player) ? GetCentroid( player ) : Vector( 0, 0, 0 );

	CBasePlayer *other = UTIL_PlayerByUserId( event->GetInt( "attacker" ) );
	CBasePlayer *victim = player;

	CBasePlayer *killer = (other && other->IsPlayer()) ? static_cast<CBasePlayer *>( other ) : NULL;

	// TODO: Update for FF if career mode or similar concept exists and how it's checked
	// if (FFGameRules()->IsCareer() && victim && !victim->IsBot() && victim->GetTeamNumber() == GetTeamNumber())
	// {
	//	GetChatter()->Say( "CommanderDown", 20.0f ); // TODO: Update chatter events for FF
	// }

	// keep track of the last player we killed
	if (killer == this)
	{
		m_lastVictimID = victim ? victim->entindex() : 0;
	}

	// react to teammate death
	if (victim && victim->GetTeamNumber() == GetTeamNumber())
	{
		// note time of death
		m_friendDeathTimestamp = gpGlobals->curtime;

		// chastise friendly fire from humans
		if (killer && !killer->IsBot() && killer->GetTeamNumber() == GetTeamNumber() && killer != this)
		{
			// GetChatter()->KilledFriend(); // Chatter system removed
		}

		if (IsAttacking())
		{
			if (GetTimeSinceLastSawEnemy() > 0.4f)
			{
				PrintIfWatched( "Rethinking my attack due to teammate death\n" );

				// allow us to sneak past windows, doors, etc
				IgnoreEnemies( 1.0f );

				// move to last known position of enemy - this could cause us to flank if 
				// the danger has changed due to our teammate's recent death
				// Ensure GetTask() returns BotTaskType and enums like BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION are correct
				SetTask( BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION, GetBotEnemy() );
				MoveTo( GetLastKnownEnemyPosition() );
				return;
			}
		}
		else	// not attacking
		{
			//
			// If we just saw a nearby friend die, and we haven't yet acquired an enemy
			// automatically acquire our dead friend's killer
			//
			// Ensure GetDisposition() returns DispositionType and enums like ENGAGE_AND_INVESTIGATE are correct
			if (GetDisposition() == ENGAGE_AND_INVESTIGATE || GetDisposition() == OPPORTUNITY_FIRE)
			{
				CBasePlayer *otherAttacker = UTIL_PlayerByUserId( event->GetInt( "attacker" ) );

				// check that attacker is an enemy (for friendly fire, etc)
				if (otherAttacker && otherAttacker->IsPlayer())
				{
					CFFPlayer *friendKiller = static_cast<CFFPlayer *>( otherAttacker );
					if (friendKiller->GetTeamNumber() != GetTeamNumber())
					{
						// check if we saw our friend die - dont check FOV - assume we're aware of our surroundings in combat
						// snipers stay put
						if (!IsSniper() && IsVisible( playerOrigin )) // IsVisible may need playerOrigin + some Z offset
						{
							// people are dying - we should hurry
							Hurry( RandomFloat( 10.0f, 15.0f ) );

							// if we're hiding with only our knife, be a little more cautious
							const float knifeAmbushChance = 50.0f;
							if (!IsHiding() || !IsUsingKnife() || RandomFloat( 0, 100 ) < knifeAmbushChance)
							{
								PrintIfWatched( "Attacking our friend's killer!\n" );
								Attack( friendKiller );
								return;
							}
						}

						// if friend was far away and we haven't seen an enemy in awhile, go to where our friend was killed
						const float longHidingTime = 20.0f;
						// Ensure GetTask() returns BotTaskType and enums like BOT_TASK_FOLLOW are correct
						if (IsHunting() || IsInvestigatingNoise() || (IsHiding() && GetTask() != BOT_TASK_FOLLOW && GetHidingTime() > longHidingTime))
						{
							const float someTime = 10.0f;
							const float farAway = 750.0f;
							if (GetTimeSinceLastSawEnemy() > someTime && (playerOrigin - GetAbsOrigin()).IsLengthGreaterThan( farAway ))
							{
								PrintIfWatched( "Checking out where our friend was killed\n" );
								MoveTo( playerOrigin, FASTEST_ROUTE );
								return;
							}
						}
					}
				}
			}
		}
	}
	else  // an enemy was killed
	{
		// forget our current noise - it may have come from the now dead enemy
		ForgetNoise();

		if (killer && killer->GetTeamNumber() == GetTeamNumber())
		{
			// only chatter about enemy kills if we see them occur, and they were the last one we see
			if (GetNearbyEnemyCount() <= 1)
			{
				// report if number of enemies left is few and we killed the last one we saw locally
				// GetChatter()->EnemiesRemaining(); // Chatter system removed

				Vector victimOrigin = (victim) ? GetCentroid( victim ) : Vector( 0, 0, 0 );
				if (IsVisible( victimOrigin, CHECK_FOV ))
				{						
					// congratulate teammates on their kills
					if (killer && killer != this)
					{
						// float delay = RandomFloat( 2.0f, 3.0f );
						// if (killer->IsBot())
						// {
						// 	if (RandomFloat( 0.0f, 100.0f ) < 40.0f)
						// 		GetChatter()->Say( "NiceShot", 3.0f, delay ); // Chatter system removed
						// }
						// else
						// {
						// 	// humans get the honorific
						// 	// TODO_FF: Update for FF if career mode or similar concept exists
						// 	// if (FFGameRules()->IsCareer())
						// 	//	GetChatter()->Say( "NiceShotCommander", 3.0f, delay ); // Chatter system removed
						// 	// else
						// 	//	GetChatter()->Say( "NiceShotSir", 3.0f, delay ); // Chatter system removed
						// }
					}
				}
			}
		}
	}
}

//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnPlayerRadio( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CFFPlayer *player = static_cast<CFFPlayer*>(UTIL_PlayerByUserId( event->GetInt( "userid" ) ));
	if ( player == this )
		return;

	//
	// Process radio events from our team
	//
	// TODO_FF: The bot's own radio response system has been removed.
	// This function previously stored information about received radio commands
	// in m_lastRadioCommand, m_radioSubject, etc., which were then processed by
	// CFFBot::RespondToRadioCommands(). Since those members and the method are gone,
	// this function body is commented out.
	// If bots need to react to player radio for informational purposes in a simpler way
	// (without their own radio command system), this function can be revised.
	/*
	if (player && player->GetTeamNumber() == GetTeamNumber() )
	{
		RadioType radioEvent = (RadioType)event->GetInt( "slot" );

		if (radioEvent != RADIO_INVALID && radioEvent != RADIO_AFFIRMATIVE && radioEvent != RADIO_NEGATIVE && radioEvent != RADIO_REPORTING_IN)
		{
			// These members are removed:
			// m_lastRadioCommand = radioEvent;
			// m_lastRadioRecievedTimestamp = gpGlobals->curtime;
			// m_radioSubject = player;
			// m_radioPosition = GetCentroid( player );
		}
	}
	*/
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnPlayerFallDamage( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false ); // player_falldamage // TODO: Update event name for FF
}


//--------------------------------------------------------------------------------------------------------------
void CFFBot::OnPlayerFootstep( IGameEvent *event )
{
	if ( !IsAlive() )
		return;

	// don't react to our own events
	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid" ) );
	if ( player == this )
		return;

	OnAudibleEvent( event, player, 1100.0f, PRIORITY_LOW, false, IS_FOOTSTEP ); // player_footstep // TODO: Update event name for FF
}
