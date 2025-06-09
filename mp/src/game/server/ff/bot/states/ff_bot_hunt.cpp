//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"
#include "ff_bot_state_hunt.h"
#include "../ff_bot.h"
#include "../ff_bot_manager.h"
#include "../../ff_player.h"
#include "../../../shared/ff/weapons/ff_weapon_base.h"
#include "../ff_gamestate.h"
#include "../nav_mesh.h"
#include "../nav_hiding_spot.h"

#include "../bot_constants.h"
#include "../bot_profile.h"
#include "../bot_util.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//--------------------------------------------------------------------------------------------------------------
void HuntState::OnEnter( CFFBot *me )
{
	if (!me) return;

	me->Run();
	me->StandUp();

    // If not specifically assassinating VIP, set to generic seek and destroy
    if (me->GetTask() != CFFBot::BOT_TASK_ASSASSINATE_VIP_FF) {
	    me->SetTask( CFFBot::BOT_TASK_SEEK_AND_DESTROY );
    }
	me->SetDisposition( CFFBot::ENGAGE_AND_INVESTIGATE );
	me->DestroyPath();
}

//--------------------------------------------------------------------------------------------------------------
void HuntState::OnUpdate( CFFBot *me )
{
	if (!me || !me->GetGameState() || !TheFFBots() || !TheNavMesh || !me->GetProfile()) return; // Removed Chatter check

    // --- VIP Assassination Logic ---
    if (me->GetTask() == CFFBot::BOT_TASK_ASSASSINATE_VIP_FF)
    {
        CFFPlayer* pVIP = me->GetGameState()->GetVIP();

        if (pVIP && me->GetGameState()->IsVIPAlive())
        {
            me->SetTaskEntity(pVIP); // Ensure task entity is up-to-date
            me->SetBotEnemy(pVIP);   // Set VIP as primary enemy

            if (me->IsVisible(pVIP, true)) // CHECK_FOV typically true
            {
                me->PrintIfWatched("HuntState (Assassin): VIP %s is visible! Attacking.\n", pVIP->GetPlayerName());
                me->Attack(pVIP); // Transition to AttackState
                return;
            }
            else
            {
                // Path to VIP's last known location (or current if available through some means)
                // For now, MoveTo their current origin if they are not visible.
                // FFGameState might need a "LastKnownVIPPosition" if VIP disappears entirely.
                me->PrintIfWatched("HuntState (Assassin): Hunting VIP %s at %s.\n", pVIP->GetPlayerName(), VecToString(pVIP->GetAbsOrigin()));
                me->MoveTo(pVIP->GetAbsOrigin(), FASTEST_ROUTE);
                return;
            }
        }
        else
        {
            me->PrintIfWatched("HuntState (Assassin): VIP is dead, invalid, or escaped. Idling.\n");
            me->Idle();
            return;
        }
    }
    // --- End VIP Assassination Logic ---

	const float huntingTooLongTime = 30.0f;
	if (gpGlobals->curtime - me->GetStateTimestamp() > huntingTooLongTime)
	{
		me->PrintIfWatched("HuntState: Hunting for too long. Idling to re-evaluate.\n" );
		me->SetRogue( false );
		me->Idle();
		return;
	}

	// CS-specific scenario logic removed here

	if (me->HeardInterestingNoise())
	{
		me->InvestigateNoise();
		return;
	}

	me->UpdateLookAround();

	if (me->GetLastKnownArea() == m_huntArea || me->UpdatePathMovement() != CFFBot::PROGRESSING)
	{
		const float earlyGameTime = 45.0f;
		if (TheFFBots()->GetElapsedRoundTime() < earlyGameTime && !me->HasVisitedEnemySpawn())
		{
			int enemyTeam = OtherTeam( me->GetTeamNumber() ); // Assuming OtherTeam helper is accessible or defined
            if (enemyTeam != TEAM_UNASSIGNED) {
			    CBaseEntity *enemySpawn = TheFFBots()->GetRandomSpawn( enemyTeam );
			    if ( enemySpawn ) m_huntArea = TheNavMesh->GetNavArea( enemySpawn->WorldSpaceCenter() );
            }
		}
		else
		{
			m_huntArea = NULL; float oldest = 0.0f; int areaCount = 0; const float minSize = 150.0f;
			if (TheNavAreas) {
				FOR_EACH_VEC( (*TheNavAreas), it )
				{
					CNavArea *area = (*TheNavAreas)[ it ]; if (!area) continue;
					++areaCount;
					Extent extent; area->GetExtent(&extent);
					if (extent.hi.x - extent.lo.x < minSize || extent.hi.y - extent.lo.y < minSize) continue;
					float age = gpGlobals->curtime - area->GetClearedTimestamp( me->GetTeamNumber() );
					if (age > oldest) { oldest = age; m_huntArea = area; }
				}
				if (areaCount > 0 && !m_huntArea)
				{
					int which = RandomInt( 0, areaCount-1 );
                    // Ensure 'which' is a valid index for TheNavAreas, which might not be contiguous if some are NULL
                    int validIdx = 0;
                    for(int chkIdx = 0; chkIdx < TheNavAreas->Count(); ++chkIdx) {
                        if ((*TheNavAreas)[chkIdx]) {
                            if (validIdx == which) {
                                m_huntArea = (*TheNavAreas)[chkIdx];
                                break;
                            }
                            validIdx++;
                        }
                    }
                    if (!m_huntArea && TheNavAreas->Count() > 0) m_huntArea = (*TheNavAreas)[0]; // Fallback
				}
			}
		}

		if (m_huntArea)
		{
			me->ComputePath( m_huntArea->GetCenter(), SAFEST_ROUTE );
		} else {
            // No hunt area found, maybe idle for a bit to reset logic
            me->PrintIfWatched("HuntState: No suitable hunt area found. Idling.\n");
            me->Idle();
            return;
        }
	}
}

//--------------------------------------------------------------------------------------------------------------
void HuntState::OnExit( CFFBot *me )
{
	// Nothing specific for now
}

[end of mp/src/game/server/ff/bot/states/ff_bot_hunt.cpp]
