//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_lurk.cpp
// Wait for victims
// Michael Booth, September 2011

#include "cbase.h"
#include "ff_player.h"
#include "ff_obj_sentrygun.h"
#include "bot/ff_bot.h"
#include "bot/behavior/spy/ff_bot_spy_lurk.h"
#include "bot/behavior/spy/ff_bot_spy_sap.h"
#include "bot/behavior/spy/ff_bot_spy_attack.h"
#include "bot/behavior/ff_bot_retreat_to_cover.h"
#include "bot/behavior/spy/ff_bot_spy_sap.h"

#include "nav_mesh.h"

extern ConVar ff_bot_path_lookahead_range;
extern ConVar ff_bot_debug_spy;


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSpyLurk::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	// cloak 
	if ( !me->m_Shared.IsStealthed() )
	{
		me->PressAltFireButton();
	}

	// disguise as the enemy team
	me->DisguiseAsMemberOfEnemyTeam();

	m_lurkTimer.Start( RandomFloat( 3.0f, 5.0f ) );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSpyLurk::Update( CFFBot *me, float interval )
{
	const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( threat && threat->GetEntity() )
	{
		CBaseObject *enemyObject = dynamic_cast< CBaseObject * >( threat->GetEntity() );
		if ( enemyObject && !enemyObject->HasSapper() && me->IsEnemy( enemyObject ) )
		{
			return SuspendFor( new CFFBotSpySap( enemyObject ), "Sapping an enemy object" );
		}
	}

	if ( me->GetEnemySentry() != NULL && !me->GetEnemySentry()->HasSapper() )
	{
		return SuspendFor( new CFFBotSpySap( me->GetEnemySentry() ), "Sapping a Sentry" );
	}

	if ( m_lurkTimer.IsElapsed() )
	{
		return Done( "Lost patience with my hiding spot" );
	}

	CTFNavArea *myArea = me->GetLastKnownArea();

	if ( !myArea )
	{
		return Continue();
	}

	// go after victims we've gotten behind
	if ( threat && threat->GetTimeSinceLastKnown() < 3.0f )
	{
		CFFPlayer *victim = ToFFPlayer( threat->GetEntity() );
		if ( victim )
		{
			if ( !victim->IsLookingTowards( me ) )
			{
				return ChangeTo( new CFFBotSpyAttack( victim ), "Going after a backstab victim" );
			}
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotSpyLurk::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	return ANSWER_NO;
}
