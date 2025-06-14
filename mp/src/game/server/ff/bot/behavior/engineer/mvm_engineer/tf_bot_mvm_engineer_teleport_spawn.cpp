//========= Copyright Valve Corporation, All rights reserved. ============//
//
//
//
//=============================================================================
#include "cbase.h"
#include "nav_mesh.h"
#include "ff_player.h"
#include "ff_obj.h"
#include "ff_gamerules.h"
#include "bot/ff_bot.h"
#include "ff_obj_sentrygun.h"
#include "bot/behavior/engineer/mvm_engineer/ff_bot_mvm_engineer_teleport_spawn.h"
#include "bot/map_entities/ff_bot_hint_entity.h"
#include "string_t.h"
#include "ff_fx.h"
#include "player_vs_environment/tf_population_manager.h"

//---------------------------------------------------------------------------------------------
CFFBotMvMEngineerTeleportSpawn::CFFBotMvMEngineerTeleportSpawn( CBaseTFBotHintEntity* pHint, bool bFirstTeleportSpawn )
{
	m_hintEntity = pHint;
	m_bFirstTeleportSpawn = bFirstTeleportSpawn;
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMvMEngineerTeleportSpawn::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	if ( !me->HasAttribute( CFFBot::TELEPORT_TO_HINT ) )
	{
		return Done( "Cannot teleport to hint with out Attributes TeleportToHint" );
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotMvMEngineerTeleportSpawn::Update( CFFBot *me, float interval )
{
	if ( !m_teleportDelay.HasStarted() )
	{
		m_teleportDelay.Start( 0.1f );
		if ( m_hintEntity )
			FFGameRules()->PushAllPlayersAway( m_hintEntity->GetAbsOrigin(), 400, 500, FF_TEAM_RED );
	}
	else if ( m_teleportDelay.IsElapsed() )
	{
		if ( !m_hintEntity )
			return Done( "Cannot teleport to hint as m_hintEntity is NULL" );

		// teleport the engineer to the sentry spawn point
		QAngle angles = m_hintEntity->GetAbsAngles();
		Vector origin = m_hintEntity->GetAbsOrigin();
		origin.z += 10.f; // move up off the around a little bit to prevent the engineer from getting stuck in the ground

		me->Teleport( &origin, &angles, NULL );

		CPVSFilter filter( origin );
		TE_TFParticleEffect( filter, 0.0, "teleported_blue", origin, vec3_angle );
		TE_TFParticleEffect( filter, 0.0, "player_sparkles_blue", origin, vec3_angle );

		if ( m_bFirstTeleportSpawn )
		{
			// notify players that engineer's teleported into the map
			TE_TFParticleEffect( filter, 0.0, "teleported_mvm_bot", origin, vec3_angle );
			me->EmitSound( "Engineer.MVM_BattleCry07" );
			m_hintEntity->EmitSound( "MVM.Robot_Engineer_Spawn" );

			if ( g_pPopulationManager )
			{
				CWave *pWave = g_pPopulationManager->GetCurrentWave();
				if ( pWave )
				{
					if ( pWave->NumEngineersTeleportSpawned() == 0 )
					{
						FFGameRules()->BroadcastSound( 255, "Announcer.MVM_First_Engineer_Teleport_Spawned" );
					}
					else
					{
						FFGameRules()->BroadcastSound( 255, "Announcer.MVM_Another_Engineer_Teleport_Spawned" );
					}

					pWave->IncrementEngineerTeleportSpawned();
				}
			}
		}

		return Done( "Teleported" );
	}

	return Continue();
}

