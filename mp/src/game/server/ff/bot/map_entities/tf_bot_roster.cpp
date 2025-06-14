//========= Copyright Valve Corporation, All rights reserved. ============//
// ff_bot_roster.cpp
// entity that dictates what classes a bot can choose when spawning
// Tom Bui, April 2010

#include "cbase.h"

#include "ff_shareddefs.h"
#include "bot/map_entities/ff_bot_roster.h"

//------------------------------------------------------------------------------

BEGIN_DATADESC( CFFBotRoster )
	DEFINE_KEYFIELD( m_teamName,								FIELD_STRING,	"team" ),
	DEFINE_KEYFIELD( m_bAllowClassChanges,						FIELD_BOOLEAN,	"allowClassChanges" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_SCOUT],			FIELD_BOOLEAN,	"allowScout" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_SNIPER],		FIELD_BOOLEAN,	"allowSniper" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_SOLDIER],		FIELD_BOOLEAN,	"allowSoldier" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_DEMOMAN],		FIELD_BOOLEAN,	"allowDemoman" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_MEDIC],			FIELD_BOOLEAN,	"allowMedic" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_HEAVYWEAPONS],	FIELD_BOOLEAN,	"allowHeavy" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_PYRO],			FIELD_BOOLEAN,	"allowPyro" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_SPY],			FIELD_BOOLEAN,	"allowSpy" ),
	DEFINE_KEYFIELD( m_bAllowedClasses[CLASS_ENGINEER],		FIELD_BOOLEAN,	"allowEngineer" ),

	DEFINE_INPUTFUNC( FIELD_STRING, "SetTeam", InputSetTeam ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowScout", InputSetAllowScout ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowSniper", InputSetAllowSniper ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowSoldier", InputSetAllowSoldier ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowDemoman", InputSetAllowDemoman ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowMedic", InputSetAllowMedic ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowHeavy", InputSetAllowHeavy ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowPyro", InputSetAllowPyro ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowSpy", InputSetAllowSpy ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetAllowEngineer", InputSetAllowEngineer ),

END_DATADESC()

LINK_ENTITY_TO_CLASS( bot_roster, CFFBotRoster );

//------------------------------------------------------------------------------

CFFBotRoster::CFFBotRoster()
{
	memset( m_bAllowedClasses, 0, sizeof( m_bAllowedClasses ) );
}

//------------------------------------------------------------------------------

void CFFBotRoster::InputSetAllowScout( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_SCOUT] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowSniper( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_SNIPER] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowSoldier( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_SOLDIER] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowDemoman( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_DEMOMAN] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowMedic( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_MEDIC] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowHeavy( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_HEAVYWEAPONS] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowPyro( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_PYRO] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowSpy( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_SPY] = inputdata.value.Bool();
}

void CFFBotRoster::InputSetAllowEngineer( inputdata_t &inputdata )
{
	m_bAllowedClasses[CLASS_ENGINEER] = inputdata.value.Bool();
}

//------------------------------------------------------------------------------

bool CFFBotRoster::IsClassAllowed( int iBotClass ) const
{
	return iBotClass > CLASS_UNDEFINED && iBotClass < CLASS_CIVILIAN && m_bAllowedClasses[iBotClass];
}

//------------------------------------------------------------------------------

bool CFFBotRoster::IsClassChangeAllowed() const
{
	return m_bAllowClassChanges;
}
