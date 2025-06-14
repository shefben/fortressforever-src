//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_nav_ent_wait.cpp
// Wait for awhile, as directed by nav entity
// Michael Booth, September 2009

#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/nav_entities/ff_bot_nav_ent_wait.h"

extern ConVar ff_bot_path_lookahead_range;

//---------------------------------------------------------------------------------------------
CFFBotNavEntWait::CFFBotNavEntWait( const CFuncNavPrerequisite *prereq )
{
	m_prereq = prereq;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotNavEntWait::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	if ( m_prereq == NULL )
	{
		return Done( "Prerequisite has been removed before we started" );
	}

	m_timer.Start( m_prereq->GetTaskValue() );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotNavEntWait::Update( CFFBot *me, float interval )
{
	if ( m_prereq == NULL )
	{
		return Done( "Prerequisite has been removed" );
	}

	if ( !m_prereq->IsEnabled() )
	{
		return Done( "Prerequisite has been disabled" );
	}

	if ( m_timer.IsElapsed() )
	{
		return Done( "Wait time elapsed" );
	}

	return Continue();
}


