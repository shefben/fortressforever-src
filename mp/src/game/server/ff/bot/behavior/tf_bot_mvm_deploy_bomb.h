//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_mvm_deploy_bomb.h
// Set us up the bomb!

#ifndef FF_BOT_MVM_DEPLOY_BOMB_H
#define FF_BOT_MVM_DEPLOY_BOMB_H

//-----------------------------------------------------------------------------
class CFFBotMvMDeployBomb : public Action< CFFBot >
{
public:
	virtual ActionResult< CFFBot >	OnStart( CFFBot *me, Action< CFFBot > *priorAction );
	virtual ActionResult< CFFBot >	Update( CFFBot *me, float interval );
	virtual void					OnEnd( CFFBot *me, Action< CFFBot > *nextAction );

	EventDesiredResult< CFFBot >	OnContact( CFFBot *me, CBaseEntity *other, CGameTrace *result );
	QueryResultType					ShouldAttack( const INextBot *me, const CKnownEntity *them ) const;

	virtual const char *GetName( void ) const	{ return "MvMDeployBomb"; };

private:
	CountdownTimer m_timer;
	Vector m_anchorPos;
};


#endif // FF_BOT_MVM_DEPLOY_BOMB_H
