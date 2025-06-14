//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_use_item.h
// Equip and consume an item
// Michael Booth, July 2011

#ifndef FF_BOT_USE_ITEM_H
#define FF_BOT_USE_ITEM_H

class CFFBotUseItem : public Action< CFFBot >
{
public:
	CFFBotUseItem( CFFWeaponBase *item );
	virtual ~CFFBotUseItem() { }

	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	virtual const char *GetName( void ) const	{ return "UseItem"; };

private:
	CHandle< CFFWeaponBase > m_item;
	CountdownTimer m_cooldownTimer;
};


#endif // FF_BOT_USE_ITEM_H
