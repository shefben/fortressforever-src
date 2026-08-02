//========= Fortress Forever Bot =============================================//
//
// CFFBotTaunt — see header.
//
//===========================================================================//

#include "cbase.h"
#include "ff_bot_taunt.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotTaunt::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	// Random short delay so a group of bots doesn't taunt in unison.
	m_startDelay.Start( RandomFloat( 0.0f, 1.0f ) );
	m_didTaunt = false;
	return Continue();
}


//-----------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotTaunt::Update( CFFBot *me, float interval )
{
	if ( !m_startDelay.IsElapsed() )
		return Continue();

	if ( !m_didTaunt )
	{
		// Hook point: when FF's taunt API is wired up, fire it here. For
		// now, just stand still for a few seconds.
		m_tauntEndTimer.Start( RandomFloat( 3.0f, 5.0f ) );
		m_didTaunt = true;
		return Continue();
	}

	if ( m_tauntEndTimer.IsElapsed() )
		return Done( "Taunt finished" );

	return Continue();
}
