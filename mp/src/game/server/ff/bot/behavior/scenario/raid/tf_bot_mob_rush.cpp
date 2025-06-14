//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_mob_rush.cpp
// A member of a rushing mob of melee attackers
// Michael Booth, October 2009

#include "cbase.h"

#ifdef TF_RAID_MODE

#include "team.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_taunt.h"
#include "bot/behavior/scenario/raid/ff_bot_mob_rush.h"


ConVar ff_bot_taunt_range( "ff_bot_taunt_range", "100", FCVAR_CHEAT );
ConVar ff_raid_mob_rush_vocalize_min_interval( "ff_raid_mob_rush_vocalize_min_interval", "5", FCVAR_CHEAT );
ConVar ff_raid_mob_rush_vocalize_max_interval( "ff_raid_mob_rush_vocalize_max_interval", "8", FCVAR_CHEAT );
ConVar ff_raid_mob_avoid_range( "ff_raid_mob_avoid_range", "100", FCVAR_CHEAT );


//---------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------
CFFBotMobRush::CFFBotMobRush( CFFPlayer *victim, float reactionTime )
{
	m_victim = victim;

	// this isn't strictly correct - we shouldn't start the timer until OnStart
	m_reactionTimer.Start( reactionTime );
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotMobRush::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	m_vocalizeTimer.Start( RandomFloat( ff_raid_mob_rush_vocalize_min_interval.GetFloat(), ff_raid_mob_rush_vocalize_max_interval.GetFloat() ) );
	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMobRush::Update( CFFBot *me, float interval )
{
	// mobs use only their melee weapons
	CBaseCombatWeapon *meleeWeapon = me->Weapon_GetSlot( TF_WPN_TYPE_MELEE );
	if ( meleeWeapon )
	{
		me->Weapon_Switch( meleeWeapon );
	}


	if ( m_victim == NULL )
	{
		return Done( "No victim" );
	}

	me->GetBodyInterface()->AimHeadTowards( m_victim, IBody::CRITICAL, 1.0f, NULL, "Looking at our melee target" );

	if ( m_reactionTimer.HasStarted() )
	{
		if ( m_reactionTimer.IsElapsed() )
		{
			// snap out of it!
			me->DoAnimationEvent( PLAYERANIMEVENT_VOICE_COMMAND_GESTURE, ACT_MP_GESTURE_VC_FINGERPOINT_PRIMARY );
			me->SpeakConceptIfAllowed( MP_CONCEPT_PLAYER_BATTLECRY );
			m_reactionTimer.Invalidate();
		}
		else
		{
			// wait for reaction time to elapse
			return Continue();
		}
	}

	if ( me->IsPlayingGesture( ACT_MP_GESTURE_VC_FINGERPOINT_PRIMARY ) )
	{
		// wait for "wake up" anim to finish
		return Continue();
	}

	// just keep swinging
	me->PressFireButton();

	// chase them down
	CFFBotPathCost cost( me, FASTEST_ROUTE );
	m_path.Update( me, m_victim, cost );

	// avoid friends
	CTeam *team = GetGlobalTeam( FF_TEAM_RED );
	for( int t=0; t<team->GetNumPlayers(); ++t )
	{
		CFFPlayer *teamMember = (CFFPlayer *)team->GetPlayer(t);

		if ( !teamMember->IsAlive() )
			continue;

		Vector toBuddy = teamMember->GetAbsOrigin() - me->GetAbsOrigin();
		if ( toBuddy.IsLengthLessThan( ff_raid_mob_avoid_range.GetFloat() ) )
		{
			float range = toBuddy.NormalizeInPlace();

			me->GetLocomotionInterface()->Approach( me->GetAbsOrigin() - 100.0f * toBuddy, 1.0f - ( range / ff_raid_mob_avoid_range.GetFloat() ) );
		}
	}

	
	if ( !m_victim->IsAlive() && me->IsRangeLessThan( m_victim, ff_bot_taunt_range.GetFloat() ) )
	{
		// we got 'em!
		return ChangeTo( new CFFBotTaunt, "Taunt their corpse" );
	}

	if ( m_vocalizeTimer.IsElapsed() )
	{
		m_vocalizeTimer.Start( RandomFloat( ff_raid_mob_rush_vocalize_min_interval.GetFloat(), ff_raid_mob_rush_vocalize_max_interval.GetFloat() ) );

		if ( me->IsPlayerClass( CLASS_SCOUT ) )
			me->EmitSound( "Scout.MobJabber" );
		else if ( me->IsPlayerClass( CLASS_HEAVYWEAPONS ) )
			me->EmitSound( "Heavy.MobJabber" );
		else
			me->SpeakConceptIfAllowed( MP_CONCEPT_PLAYER_BATTLECRY );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMobRush::OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result )
{
	return TryToSustain( RESULT_CRITICAL, "Ignoring contact" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMobRush::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	return TryToSustain( RESULT_CRITICAL, "Ignoring injury" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMobRush::OnOtherKilled( CFFBot *me, CBaseCombatCharacter *victim, const CTakeDamageInfo &info )
{
	return TryToSustain( RESULT_CRITICAL, "Ignoring friend death" );
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotMobRush::OnStuck( CFFBot *me )
{
	m_path.Invalidate();
	return TryToSustain( RESULT_CRITICAL );
}


//---------------------------------------------------------------------------------------------
QueryResultType	CFFBotMobRush::ShouldRetreat( const INextBot *me ) const
{
	return ANSWER_NO;
}

#endif // TF_RAID_MODE
