//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_use_item.h
// Equip and consume an item
// Michael Booth, July 2011

#include "cbase.h"
#include "ff_weaponbase.h"
#include "bot/ff_bot.h"
#include "bot/behavior/ff_bot_use_item.h"


//---------------------------------------------------------------------------------------------
CFFBotUseItem::CFFBotUseItem( CTFWeaponBase *item )
{
	m_item = item;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotUseItem::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	// force-equip the item we're going to use
	me->PushRequiredWeapon( m_item );

	m_cooldownTimer.Start( m_item->m_flNextPrimaryAttack - gpGlobals->curtime + 0.25f );

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotUseItem::Update( CFFBot *me, float interval )
{
	if ( m_item == NULL )
	{
		return Done( "NULL item" );
	}

	CTFWeaponBase *myCurrentWeapon = me->m_Shared.GetActiveTFWeapon();

	if ( !myCurrentWeapon )
	{
		return Done( "NULL weapon" );
	}

	if ( m_cooldownTimer.HasStarted() )
	{
		if ( m_cooldownTimer.IsElapsed() )
		{
			// use it
			me->PressFireButton();
			m_cooldownTimer.Invalidate();
		}
	}
	else // used
	{
		// some items use the taunt system - wait for the taunt to end
		if ( !me->IsTaunting() )
		{
			return Done( "Item used" );
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
void CFFBotUseItem::OnEnd( CFFBot *me, Action< CFFBot > *nextAction )
{
	me->PopRequiredWeapon();
}

