//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_wander.cpp
// Wanderering/idle enemies for Squad Co-op mode
// Michael Booth, October 2009

#include "cbase.h"

#ifdef TF_RAID_MODE

#include "team.h"
#include "raid/ff_raid_logic.h"
#include "bot/ff_bot.h"
#include "bot/behavior/scenario/raid/ff_bot_wander.h"
#include "bot/behavior/scenario/raid/ff_bot_mob_rush.h"


ConVar ff_raid_wanderer_aggro_range( "ff_raid_wanderer_aggro_range", "500", FCVAR_CHEAT, "If wanderers see a threat closer than this, they attack" );
ConVar ff_raid_wanderer_notice_friend_death_range( "ff_raid_wanderer_notice_friend_death_range", "1000", FCVAR_CHEAT, "If a friend dies within this radius of a wanderer, it wakes up and attacks the attacker" );
ConVar ff_raid_wanderer_reaction_factor( "ff_raid_wanderer_reaction_factor", "1", FCVAR_CHEAT );
ConVar ff_raid_wanderer_vocalize_min_interval( "ff_raid_wanderer_vocalize_min_interval", "20", FCVAR_CHEAT );
ConVar ff_raid_wanderer_vocalize_max_interval( "ff_raid_wanderer_vocalize_max_interval", "30", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
CFFBotWander::CFFBotWander( void )
{
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotWander::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_vocalizeTimer.Start( RandomFloat( ff_raid_wanderer_vocalize_min_interval.GetFloat(), ff_raid_wanderer_vocalize_max_interval.GetFloat() ) );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotWander::Update( CFFBot *me, float interval )
{
	// mobs use only their melee weapons
	CBaseCombatWeapon *meleeWeapon = me->Weapon_GetSlot( TF_WPN_TYPE_MELEE );
	if ( meleeWeapon )
	{
		me->Weapon_Switch( meleeWeapon );
	}


	CTeam *raidingTeam = GetGlobalTeam( FF_TEAM_BLUE );

	if ( me->HasAttribute( CFFBot::AGGRESSIVE ) )
	{
		// I'm a mob rusher - pick a random raider and attack them!
		CFFPlayer *victim = TFGameRules()->GetRaidLogic()->SelectRaiderToAttack();
		if ( victim )
		{
			return SuspendFor( new CFFBotMobRush( victim ), "Rushing a raider" );
		}
	}
	else if ( m_visionTimer.IsElapsed() )
	{
		// I'm a wanderer - look for very nearby threats
		m_visionTimer.Start( RandomFloat( 0.5f, 1.0f ) );

		// find closest visible raider within aggro range
		CFFPlayer *threat = NULL;
		float closeThreatRangeSq = ff_raid_wanderer_aggro_range.GetFloat() * ff_raid_wanderer_aggro_range.GetFloat();

		for( int i=0; i<raidingTeam->GetNumPlayers(); ++i )
		{
			CFFPlayer *player = (CFFPlayer *)raidingTeam->GetPlayer(i);

			if ( !player->IsAlive() )
				continue;

			float rangeSq = me->GetRangeSquaredTo( player );
			if ( rangeSq < closeThreatRangeSq )
			{
				if ( me->GetVisionInterface()->IsLineOfSightClearToEntity( player ) )
				{
					threat = player;
					closeThreatRangeSq = rangeSq;
				}
			}
		}

		if ( threat )
		{
			return SuspendFor( new CFFBotMobRush( threat ), "Attacking threat!" );
		}
	}

	if ( m_vocalizeTimer.IsElapsed() )
	{
		m_vocalizeTimer.Start( RandomFloat( ff_raid_wanderer_vocalize_min_interval.GetFloat(), ff_raid_wanderer_vocalize_max_interval.GetFloat() ) );

		// mouth off
		if ( me->IsPlayerClass( CLASS_SCOUT ) )
			me->EmitSound( "Scout.WanderJabber" );
		else
			me->SpeakConceptIfAllowed( MP_CONCEPT_PLAYER_JEERS );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotWander::OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result  )
{
	if ( other && other->IsPlayer() && me->IsEnemy( other ) )
	{
		return TrySuspendFor( new CFFBotMobRush( (CFFPlayer *)other ), RESULT_IMPORTANT, "Attacking threat who touched me!" );
	}

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotWander::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	if ( info.GetAttacker() && info.GetAttacker()->IsPlayer() && me->IsEnemy( info.GetAttacker() ) )
	{
		return TrySuspendFor( new CFFBotMobRush( (CFFPlayer *)info.GetAttacker() ), RESULT_IMPORTANT, "Attacking threat who attacked me!" );
	}

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotWander::OnOtherKilled( CFFBot *me, CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	if ( victim && me->IsFriend( victim ) )
	{
		if ( info.GetAttacker() && info.GetAttacker()->IsPlayer() && me->IsEnemy( info.GetAttacker() ) )
		{
			if ( me->IsRangeLessThan( victim, ff_raid_wanderer_notice_friend_death_range.GetFloat() ) )
			{
				if ( me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) && 
					 me->GetVisionInterface()->IsAbleToSee( info.GetAttacker(), IVision::DISREGARD_FOV ) )
				{
					float rangeToAttacker = me->GetRangeTo( info.GetAttacker() );
					float reactionTime;

					if ( rangeToAttacker < ff_raid_wanderer_aggro_range.GetFloat() )
					{
						reactionTime = 0.0f;
					}
					else
					{
						reactionTime = ff_raid_wanderer_reaction_factor.GetFloat() * ( rangeToAttacker - ff_raid_wanderer_aggro_range.GetFloat() ) / ff_raid_wanderer_aggro_range.GetFloat();
					}

					return TrySuspendFor( new CFFBotMobRush( (CFFPlayer *)info.GetAttacker(), reactionTime ), RESULT_IMPORTANT, "Attacking my friend's attacker!" );
				}
			}
		}
	}

	return TryContinue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotWander::OnCommandAttack( CFFBot *me, CBaseEntity *victim )
{
	return TryContinue();
}


#endif // TF_RAID_MODE
