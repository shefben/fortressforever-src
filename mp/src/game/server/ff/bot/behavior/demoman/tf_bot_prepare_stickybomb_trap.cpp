//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_prepare_stickybomb_trap.cpp
// Place stickybombs to create a deadly trap
// Michael Booth, July 2010

#include "cbase.h"
#include "ff_player.h"
#include "bot/ff_bot.h"
#include "bot/behavior/demoman/ff_bot_prepare_stickybomb_trap.h"
#include "ff_weapon_pipebomblauncher.h"

#define MAX_STICKYBOMB_COUNT 8

ConVar ff_bot_stickybomb_density( "ff_bot_stickybomb_density", "0.0001", FCVAR_CHEAT, "Number of stickies to place per square inch" );


//---------------------------------------------------------------------------------------------
class PlaceStickyBombReply : public INextBotReply
{
public:
	virtual void OnSuccess( INextBot *bot )		// invoked when process completed successfully
	{
		CFFBot *me = ToTFBot( bot->GetEntity() );

		CTFWeaponBase *myCurrentWeapon = me->m_Shared.GetActiveTFWeapon();
		if ( myCurrentWeapon && myCurrentWeapon->GetWeaponID() == FF_WEAPON_PIPEBOMBLAUNCHER )
		{
			// launch the sticky
			me->PressFireButton( 0.1f );

			// increase the bomb count for this target area
			if ( m_bombTargetArea )
			{
				m_bombTargetArea->m_count++;
			}

			if( m_pLaunchWaitTimer )
			{
				// release the latch
				m_pLaunchWaitTimer->Start( 0.15f );
			}
		}
	}

	virtual void OnFail( INextBot *bot, FailureReason reason )// invoked when process failed
	{
		// retry aim immediately
		m_pLaunchWaitTimer->Invalidate();
	}

	void ClearData()
	{
		// Be sure to clear all members here, as we can potentially get an OnSuccess() call
		//  after the ~CFFBotPrepareStickybombTrap.
		m_bombTargetArea = NULL;
		m_pLaunchWaitTimer = NULL;
	}

	CFFBotPrepareStickybombTrap::BombTargetArea *m_bombTargetArea;
	CountdownTimer *m_pLaunchWaitTimer;
};


static PlaceStickyBombReply bombReply;


//---------------------------------------------------------------------------------------------
CFFBotPrepareStickybombTrap::CFFBotPrepareStickybombTrap( void )
{
	m_myArea = NULL;
}


//---------------------------------------------------------------------------------------------
CFFBotPrepareStickybombTrap::~CFFBotPrepareStickybombTrap( )
{
	bombReply.ClearData();
}


//---------------------------------------------------------------------------------------------
// Return true if this Action has what it needs to perform right now
bool CFFBotPrepareStickybombTrap::IsPossible( CFFBot *me )
{
	// don't lay a trap if we're in the midst of fighting
	if ( /*me->IsInCombat() || */ me->GetTimeSinceLastInjury() < 1.0f )
	{
		return false;
	}

	if ( !me->IsPlayerClass( CLASS_DEMOMAN ) )
	{
		return false;
	}

	CTFPipebombLauncher *stickyLauncher = dynamic_cast< CTFPipebombLauncher * >( me->Weapon_GetSlot( TF_WPN_TYPE_SECONDARY ) );
	if ( stickyLauncher && !me->IsWeaponRestricted( stickyLauncher ) )
	{
		if ( stickyLauncher->GetPipeBombCount() >= MAX_STICKYBOMB_COUNT || me->GetAmmoCount( TF_AMMO_SECONDARY ) <= 0 )
		{
			return false;
		}
	}

	return true;
}


//---------------------------------------------------------------------------------------------
void CFFBotPrepareStickybombTrap::InitBombTargetAreas( CFFBot *me )
{
	const CUtlVector< CTFNavArea * > &invasionAreaVector = m_myArea->GetEnemyInvasionAreaVector( me->GetTeamNumber() );

	// randomly shuffle the target areas
	CUtlVector< CTFNavArea * > shuffleVector;
	shuffleVector = invasionAreaVector;
	int n = shuffleVector.Count();
	while( n > 1 )
	{
		int k = RandomInt( 0, n-1 );
		n--;

		CTFNavArea *tmp = shuffleVector[n];
		shuffleVector[n] = shuffleVector[k];
		shuffleVector[k] = tmp;
	}

	// initialize each target area to zero sticky bombs
	m_bombTargetAreaVector.RemoveAll();

	for( int i=0; i<shuffleVector.Count(); ++i )
	{
		BombTargetArea target;
		target.m_area = shuffleVector[i];
		target.m_count = 0;

		m_bombTargetAreaVector.AddToTail( target );
	}

	m_launchWaitTimer.Invalidate();

	// Clean up any in-flight AimHeadTowards() replies, since changing m_bombTargetAreaVector
	// might move memory and invalidate the current reply pointer.
	me->GetBodyInterface()->ClearPendingAimReply();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotPrepareStickybombTrap::OnStart( CFFBot *me, Action< CFFBot > *priorAction )
{
	// detonate old set of stickies
	// me->PressAltFireButton();

	// reload entire clip before laying sticky trap
	CTFPipebombLauncher *stickyLauncher = dynamic_cast< CTFPipebombLauncher * >( me->Weapon_GetSlot( TF_WPN_TYPE_SECONDARY ) );
	if ( stickyLauncher )
	{
		m_isFullReloadNeeded = ( me->GetAmmoCount( TF_AMMO_SECONDARY ) >= stickyLauncher->GetMaxClip1() && stickyLauncher->Clip1() < stickyLauncher->GetMaxClip1() );
	}
	else
	{
		m_isFullReloadNeeded = false;
	}

	m_myArea = me->GetLastKnownArea();
	if ( !m_myArea )
	{
		return Done( "No nav mesh" );
	}

	InitBombTargetAreas( me );

	// own our view updating so we can aim
	me->StopLookingAroundForEnemies();

	return Continue();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot >	CFFBotPrepareStickybombTrap::Update( CFFBot *me, float interval )
{
	if ( !TFGameRules()->InSetup() )
	{
		const CKnownEntity *threat = me->GetVisionInterface()->GetPrimaryKnownThreat();
		if ( threat )
		{
			const float giveUpRange = 500.0f;
			if ( me->IsDistanceBetweenLessThan( threat->GetLastKnownPosition(), giveUpRange ) )
			{
				return Done( "Enemy nearby - giving up" );
			}
		}
	}

	if ( me->GetLastKnownArea() && me->GetLastKnownArea() != m_myArea )
	{
		// we've moved
		m_myArea = me->GetLastKnownArea();
		InitBombTargetAreas( me );
	}

	CTFWeaponBase *myCurrentWeapon = me->m_Shared.GetActiveTFWeapon();
	CTFPipebombLauncher *stickyLauncher = dynamic_cast< CTFPipebombLauncher * >( me->Weapon_GetSlot( TF_WPN_TYPE_SECONDARY ) );

	if ( !myCurrentWeapon || !stickyLauncher )
	{
		return Done( "Missing weapon" );
	}

	if ( myCurrentWeapon->GetWeaponID() != FF_WEAPON_PIPEBOMBLAUNCHER )
	{
		me->Weapon_Switch( stickyLauncher );
	}

	// reload fully
	if ( m_isFullReloadNeeded )
	{
		int maxClip = MIN( stickyLauncher->GetMaxClip1(), me->GetAmmoCount( TF_AMMO_SECONDARY ) );

		if ( stickyLauncher->Clip1() >= maxClip )
		{
			// fully reloaded
			m_isFullReloadNeeded = false;
		}

		me->PressReloadButton();

		return Continue();
	}


	if ( stickyLauncher->GetPipeBombCount() >= MAX_STICKYBOMB_COUNT || me->GetAmmoCount( TF_AMMO_SECONDARY ) <= 0 )
	{
		return Done( "Max sticky bombs reached" );
	}


	// aim towards areas where enemy will come from
	if ( m_launchWaitTimer.IsElapsed() )
	{
		// find next target that needs bombs
		int i;
		for( i=0; i<m_bombTargetAreaVector.Count(); ++i )
		{
			CTFNavArea *targetArea = m_bombTargetAreaVector[i].m_area;

			int desiredCount = ff_bot_stickybomb_density.GetFloat() * targetArea->GetSizeX() * targetArea->GetSizeY();
			if ( desiredCount < 1 )
			{
				desiredCount = 1;
			}

			if ( m_bombTargetAreaVector[i].m_count < desiredCount )
			{
				// place a sticky on this area
				bombReply.m_bombTargetArea = &m_bombTargetAreaVector[i];

				// this timer causes us to wait until the aim finishes and launched before we start another aim
				m_launchWaitTimer.Start( 2.0f );
				bombReply.m_pLaunchWaitTimer = &m_launchWaitTimer;

				Vector bombSpot = targetArea->GetRandomPoint();

				me->GetBodyInterface()->AimHeadTowards( bombSpot, IBody::IMPORTANT, 5.0f, &bombReply, "Aiming a sticky bomb" );

				break;
			}
		}

		if ( i == m_bombTargetAreaVector.Count() )
		{
			return Done( "Exhausted bomb target areas" );
		}
	}

	return Continue();
}


//---------------------------------------------------------------------------------------------
void CFFBotPrepareStickybombTrap::OnEnd( CFFBot *me, Action< CFFBot > *nextAction )
{
	// clean up any in-flight AimHeadTowards() replies
	me->GetBodyInterface()->ClearPendingAimReply();

	me->StartLookingAroundForEnemies();
}


//---------------------------------------------------------------------------------------------
ActionResult< CFFBot > CFFBotPrepareStickybombTrap::OnSuspend( CFFBot *me, Action< CFFBot > *interruptingAction )
{
	// this behavior is transitory - if we need to do something else, just give up
	return Done();
}


//---------------------------------------------------------------------------------------------
EventDesiredResult< CFFBot > CFFBotPrepareStickybombTrap::OnInjured( CFFBot *me, const CTakeDamageInfo &info )
{
	return TryDone( RESULT_IMPORTANT, "Ouch!" );
}


//---------------------------------------------------------------------------------------------
QueryResultType CFFBotPrepareStickybombTrap::ShouldAttack( const INextBot *me, const CKnownEntity *them ) const
{
	return ANSWER_NO;
}
