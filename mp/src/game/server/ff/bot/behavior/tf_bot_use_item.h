//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_use_item.h
// Equip and consume an item
// Michael Booth, July 2011

#ifndef FF_BOT_USE_ITEM_H
#define FF_BOT_USE_ITEM_H

class CTFBotUseItem : public Action< CTFBot >
{
public:
	CTFBotUseItem( CTFWeaponBase *item );
	virtual ~CTFBotUseItem() { }

	virtual ActionResult< CTFBot >	OnStart( CTFBot *me, Action< CTFBot > *priorAction );
	virtual ActionResult< CTFBot >	Update( CTFBot *me, float interval );
	virtual void					OnEnd( CTFBot *me, Action< CTFBot > *nextAction );

	virtual const char *GetName( void ) const	{ return "UseItem"; };

private:
	CHandle< CTFWeaponBase > m_item;
	CountdownTimer m_cooldownTimer;
};


#endif // FF_BOT_USE_ITEM_H
