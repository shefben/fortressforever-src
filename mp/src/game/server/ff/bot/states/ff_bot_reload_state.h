//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bot state for reloading their active weapon.
//
//=============================================================================//

#ifndef FF_BOT_RELOAD_STATE_H
#define FF_BOT_RELOAD_STATE_H
#ifdef _WIN32
#pragma once
#endif

#include "../ff_bot.h" // For BotState and CFFBot
#include "../ff_weapon_base.h" // For CFFWeaponBase

class ReloadState : public BotState
{
public:
	ReloadState(void);
	virtual void OnEnter( CFFBot *me );
	virtual void OnUpdate( CFFBot *me );
	virtual void OnExit( CFFBot *me );
	virtual const char *GetName( void ) const { return "ReloadState"; }

private:
	CountdownTimer m_reloadDurationTimer;
	CHandle<CFFWeaponBase> m_weaponToReload; // Store a handle in case weapon is dropped/changed
};

#endif // FF_BOT_RELOAD_STATE_H
