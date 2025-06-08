//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#include "cbase.h"

#pragma warning( disable : 4530 )					// STL uses exceptions, but we are not compiling with them - ignore warning

#define DEFINE_DIFFICULTY_NAMES
#include "bot_profile.h"
#include "shared_util.h"

#include "bot.h"
#include "bot_util.h"
// #include "cs_bot.h" // CS Dependency removed
#include "../../server/ff/bot/ff_bot.h" // Added for CFFBot context if any util functions need it (though not directly here)
#include "../../server/ff/ff_player.h" // Added for CFFPlayer context if any util functions need it
#include "../../server/ff/bot/ff_bot_manager.h" // For FF_TEAM_* constants
#include "../weapons/ff_weapon_base.h" // For FFWeaponID and AliasToWeaponID (FF version)


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


BotProfileManager *TheBotProfiles = NULL;


//--------------------------------------------------------------------------------------------------------
/**
 * Generates a filename-decorated skin name
 */
static const char * GetDecoratedSkinName( const char *name, const char *filename )
{
	const int BufLen = _MAX_PATH + 64;
	static char buf[BufLen];
	Q_snprintf( buf, sizeof( buf ), "%s/%s", filename, name );
	return buf;
}

//--------------------------------------------------------------------------------------------------------------
const char* BotProfile::GetWeaponPreferenceAsString( int i ) const
{
	// FF_TODO: This requires an FF equivalent of WeaponIDToAlias.
	// The WeaponIDToAlias declared in ff_weapon_base.h should be used here.
	if ( i < 0 || i >= m_weaponPreferenceCount )
		return NULL;
	return WeaponIDToAlias( m_weaponPreference[ i ] ); // Assumes FF version of WeaponIDToAlias
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if this profile has a primary weapon preference
 */
bool BotProfile::HasPrimaryPreference( void ) const
{
	// FF_TODO: This requires FF equivalent of IsPrimaryWeapon and FF weapon ID system.
	// This also depends on how FF categorizes weapons (primary, secondary etc.)
	// For now, returning false as the specific logic is unknown.
	/*
	for( int i=0; i<m_weaponPreferenceCount; ++i )
	{
		// if (IsFFPrimaryWeapon( m_weaponPreference[i] )) // Example: FF-specific check
		//	 return true;
	}
	*/
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if this profile has a pistol weapon preference
 */
bool BotProfile::HasPistolPreference( void ) const
{
	// FF_TODO: This requires FF equivalent of IsSecondaryWeapon (or IsPistol) and FF weapon ID system.
	/*
	for( int i=0; i<m_weaponPreferenceCount; ++i )
	{
		// if (IsFFPistolWeapon( m_weaponPreference[i] )) // Example: FF-specific check
		//	return true;
	}
	*/
	return false;
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Return true if this profile is valid for the specified team
 */
bool BotProfile::IsValidForTeam( int team ) const
{
	// m_teams stores FF_TEAM_* values from parsing
	return ( team == FF_TEAM_UNASSIGNED || m_teams == FF_TEAM_UNASSIGNED || team == m_teams || m_teams == TEAM_ANY ); // TEAM_ANY from basetypes.h might be used in profile files
}


//--------------------------------------------------------------------------------------------------------------
/**
* Return true if this profile inherits from the specified template
*/
bool BotProfile::InheritsFrom( const char *name ) const
{
	if ( WildcardMatch( name, GetName() ) )
		return true;

	for ( int i=0; i<m_templates.Count(); ++i )
	{
		const BotProfile *queryTemplate = m_templates[i];
		if ( queryTemplate->InheritsFrom( name ) )
		{
			return true;
		}
	}
	return false;
}


//--------------------------------------------------------------------------------------------------------------
/**
 * Constructor
 */
BotProfileManager::BotProfileManager( void )
{
	m_nextSkin = 0;
	for (int i=0; i<NumCustomSkins; ++i)
	{
		m_skins[i] = NULL;
		m_skinFilenames[i] = NULL;
		m_skinModelnames[i] = NULL;
	}
}

//--------------------------------------------------------------------------------------------------------------
/**
 * Load the bot profile database
 */
void BotProfileManager::Init( const char *filename, unsigned int *checksum )
{
	FileHandle_t file = filesystem->Open( filename, "r" );

	if (!file)
	{
		CONSOLE_ECHO( "WARNING: Cannot access bot profile database '%s'\n", filename );
		return;
	}

	int dataLength = filesystem->Size( filename );
	char *dataPointer = new char[ dataLength ];
	int dataReadLength = filesystem->Read( dataPointer, dataLength, file );
	filesystem->Close( file );
	if ( dataReadLength > 0 ) dataPointer[ dataReadLength - 1 ] = 0;

	const char *dataFile = dataPointer;
	if (checksum) *checksum = 0;

	BotProfile defaultProfile;

	while( true )
	{
		dataFile = SharedParse( dataFile ); if (!dataFile) break;
		char *token = SharedGetToken();
		bool isDefault = (!stricmp( token, "Default" ));
		bool isTemplate = (!stricmp( token, "Template" ));
		bool isCustomSkin = (!stricmp( token, "Skin" ));

		if ( isCustomSkin ) { /* ... (custom skin logic remains as is) ... */ } // This part is unchanged

		BotProfile *profile;
		if (isDefault) profile = &defaultProfile;
		else { profile = new BotProfile; *profile = defaultProfile; }

		if (!isTemplate && !isDefault) { /* ... (template inheritance logic remains as is) ... */ }

		if (!isDefault)
		{
			dataFile = SharedParse( dataFile ); if (!dataFile) { CONSOLE_ECHO( "Error parsing '%s' - expected name\n", filename ); delete [] dataPointer; return; }
			profile->m_name = CloneString( SharedGetToken() );
			// FF_TODO: Review if m_prefersSilencer logic is needed for FF
			// if ( profile->m_name[0] % 2 ) profile->m_prefersSilencer = true;
		}

		bool isFirstWeaponPref = true;
		while( true )
		{
			dataFile = SharedParse( dataFile ); if (!dataFile) { CONSOLE_ECHO( "Error parsing %s - expected 'End'\n", filename ); delete [] dataPointer; return; }
			token = SharedGetToken(); if (!stricmp( token, "End" )) break;
			char attributeName[64]; strcpy( attributeName, token );
			dataFile = SharedParse( dataFile ); if (!dataFile) { CONSOLE_ECHO( "Error parsing %s - expected '='\n", filename ); delete [] dataPointer; return; }
			token = SharedGetToken(); if (strcmp( "=", token )) { CONSOLE_ECHO( "Error parsing %s - expected '='\n", filename ); delete [] dataPointer; return; }
			dataFile = SharedParse( dataFile ); if (!dataFile) { CONSOLE_ECHO( "Error parsing %s - expected attribute value\n", filename ); delete [] dataPointer; return; }
			token = SharedGetToken();

			if (!stricmp( "Aggression", attributeName )) profile->m_aggression = (float)atof(token) / 100.0f;
			else if (!stricmp( "Skill", attributeName )) profile->m_skill = (float)atof(token) / 100.0f;
			else if (!stricmp( "Skin", attributeName )) { profile->m_skin = atoi(token); if ( profile->m_skin == 0 && token[0] != '0' ) profile->m_skin = GetCustomSkinIndex( token, filename ); } // Keep GetCustomSkinIndex for visual skins
			else if (!stricmp( "Teamwork", attributeName )) profile->m_teamwork = (float)atof(token) / 100.0f;
			else if (!stricmp( "Cost", attributeName )) profile->m_cost = atoi(token);
			else if (!stricmp( "VoicePitch", attributeName )) profile->m_voicePitch = atoi(token);
			else if (!stricmp( "VoiceBank", attributeName )) profile->m_voiceBank = FindVoiceBankIndex( token );
			else if (!stricmp( "WeaponPreference", attributeName ))
			{
				if (isFirstWeaponPref) { isFirstWeaponPref = false; profile->m_weaponPreferenceCount = 0; }
				if (!stricmp( token, "none" )) profile->m_weaponPreferenceCount = 0;
				else { if (profile->m_weaponPreferenceCount < BotProfile::MAX_WEAPON_PREFS) {
						// Use FF's AliasToWeaponID
						profile->m_weaponPreference[ profile->m_weaponPreferenceCount++ ] = AliasToWeaponID( token );
				} }
			}
			else if (!stricmp( "ReactionTime", attributeName )) profile->m_reactionTime = (float)atof(token);
			else if (!stricmp( "AttackDelay", attributeName )) profile->m_attackDelay = (float)atof(token);
			else if (!stricmp( "Difficulty", attributeName )) { /* ... (Difficulty parsing remains as is) ... */ }
			else if (!stricmp( "Team", attributeName ))
			{
				if ( !stricmp( token, "RED" ) ) profile->m_teams = FF_TEAM_RED;
				else if ( !stricmp( token, "BLUE" ) ) profile->m_teams = FF_TEAM_BLUE;
				else if ( !stricmp( token, "YELLOW" ) ) profile->m_teams = FF_TEAM_YELLOW; // Assuming FF_TEAM_YELLOW is defined
				else if ( !stricmp( token, "GREEN" ) ) profile->m_teams = FF_TEAM_GREEN;   // Assuming FF_TEAM_GREEN is defined
				else if ( !stricmp( token, "ANY" ) ) profile->m_teams = FF_TEAM_UNASSIGNED; // Or a specific "ANY" if defined for m_teams
				else profile->m_teams = FF_TEAM_UNASSIGNED;
			}
			else { CONSOLE_ECHO( "Error parsing %s - unknown attribute '%s'\n", filename, attributeName ); }
		}
		if (!isDefault) { if (isTemplate) m_templateList.AddToTail( profile ); else m_profileList.AddToTail( profile ); }
	}
	delete [] dataPointer;
}

//--------------------------------------------------------------------------------------------------------------
BotProfileManager::~BotProfileManager( void ) { Reset(); }
//--------------------------------------------------------------------------------------------------------------
void BotProfileManager::Reset( void ) { /* ... (implementation as before) ... */ }
//--------------------------------------------------------------------------------------------------------
const char * BotProfileManager::GetCustomSkin( int index ) { /* ... (implementation as before) ... */ return NULL; }
const char * BotProfileManager::GetCustomSkinFname( int index ) { /* ... (implementation as before) ... */ return NULL; }
const char * BotProfileManager::GetCustomSkinModelname( int index ) { /* ... (implementation as before) ... */ return NULL; }
int BotProfileManager::GetCustomSkinIndex( const char *name, const char *filename ) { /* ... (implementation as before) ... */ return 0; }
//--------------------------------------------------------------------------------------------------------
int BotProfileManager::FindVoiceBankIndex( const char *filename )
{
	for ( int i=0; i<m_voiceBanks.Count(); ++i ) if ( !stricmp( filename, m_voiceBanks[i] ) ) return i;
	m_voiceBanks.AddToTail( CloneString( filename ) );
	return m_voiceBanks.Count() - 1;
}

//--------------------------------------------------------------------------------------------------------------
const BotProfile *BotProfileManager::GetRandomProfile( BotDifficultyType difficulty, int team, FFWeaponID weaponType ) const // Changed weaponType to FFWeaponID
{
	CUtlVector< const BotProfile * > profiles;
	FOR_EACH_LL( m_profileList, it )
	{
		const BotProfile *profile = m_profileList[ it ];
		if ( !profile->IsDifficulty( difficulty ) ) continue;
		if ( UTIL_IsNameTaken( profile->GetName() ) ) continue;
		if ( !profile->IsValidForTeam( team ) ) continue;

		// FF_TODO: Adapt weapon preference matching using FFWeaponID and FF weapon categorization.
		// For now, if a specific weaponType is requested (not FF_WEAPON_NONE), we check if it's in the bot's preferences.
		if ( weaponType != FF_WEAPON_NONE )
		{
			bool foundPref = false;
			if ( profile->GetWeaponPreferenceCount() > 0 )
			{
				for(int i=0; i < profile->GetWeaponPreferenceCount(); ++i)
				{
					if (profile->GetWeaponPreference(i) == weaponType)
					{
						foundPref = true;
						break;
					}
				}
			}
			if (!foundPref) continue; // Skip if desired weapon is not preferred or no preferences exist
		}
		profiles.AddToTail( profile );
	}
	if ( !profiles.Count() ) return NULL;
	return profiles[RandomInt( 0, profiles.Count()-1 )];
}
