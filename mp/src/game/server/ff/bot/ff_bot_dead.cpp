//========= Fortress Forever Bot =============================================//
//
// CFFBotDead — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_dead.h"
#include "ff_bot_main_action.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDead::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	// Press the fire button so PlayerDeathThink calls respawn() once the
	// spawn-delay window opens. CFFBotMainAction::HandleRespawnInput does
	// the same; we keep it here too because CFFBotMainAction's Update no
	// longer runs while we're in this action.
	me->PressFireButton( 0.1f );

	return Continue();
}


//-----------------------------------------------------------------------------
// While dead, just wait. On respawn (IsAlive returns true), replace
// ourselves with a fresh CFFBotMainAction — every respawn starts the
// behavior tree from a clean slate, the same way TFBot does it. This
// removes the need for "did we just respawn?" transition tracking inside
// CFFBotCtfObjective.
//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotDead::Update( CFFBot *me, float interval )
{
	if ( me->IsAlive() )
	{
		return ChangeTo( new CFFBotMainAction, "Respawned" );
	}

	// Keep tickling the fire button so the respawn fires as soon as the
	// game's spawn-delay window allows it.
	me->PressFireButton( 0.1f );

	return Continue();
}
