//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_spy_backstab.cpp
// Chase behind a victim and backstab them
// Michael Booth, June 2010

#include "cbase.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/spy/ff_bot_spy_backstab.h"

extern ConVar ff_bot_path_lookahead_range;

//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotSpyBackstab::OnStart( CTFBot *me, Action< CTFBot > *priorAction )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CTFBot >	CTFBotSpyBackstab::Update( CTFBot *me, float interval )
{
	return Continue();
}


//---------------------------------------------------------------------------------------------
QueryResultType CTFBotSpyBackstab::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	return ANSWER_NO;
}
