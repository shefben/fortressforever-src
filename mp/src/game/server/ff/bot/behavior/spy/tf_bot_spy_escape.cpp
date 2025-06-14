//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_escape.cpp
// Flee!
// Michael Booth, June 2010

#include "cbase.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/spy/ff_bot_spy_escape.h"

extern ConVar ff_bot_path_lookahead_range;

//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSpyEscape::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotSpyEscape::Update( CFFBot *me, float interval )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotSpyEscape::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	return ANSWER_NO;
}
