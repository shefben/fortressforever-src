//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

// Author: Michael S. Booth (mike@turtlerockstudios.com), 2003

#ifndef _BOT_PROFILE_H_
#define _BOT_PROFILE_H_

#pragma warning( disable : 4786 )	// long STL names get truncated in browse info.

#include "bot_constants.h"
#include "bot_util.h"
#include "../../server/ff/ff_shareddefs.h" // For FF_TEAM_*
#include "../../server/ff/bot/ff_bot_manager.h" // For FF_TEAM_* constants
#include "../weapons/ff_weapon_base.h" // For FFWeaponID

enum
{
	FirstCustomSkin = 100,
	NumCustomSkins = 100,
	LastCustomSkin = FirstCustomSkin + NumCustomSkins - 1,
};

	
//--------------------------------------------------------------------------------------------------------------
/**
 * A BotProfile describes the "personality" of a given bot
 */
class BotProfile
{
public:
	BotProfile( void )
	{
		m_name = NULL;
		m_aggression = 0.0f;
		m_skill = 0.0f;	
		m_teamwork = 0.0f;
		m_weaponPreferenceCount = 0;
		m_cost = 0;
		m_skin = 0;	
		m_difficultyFlags = 0;
		m_voicePitch = 100;
		m_reactionTime = 0.3f;
		m_attackDelay = 0.0f;
		m_teams = FF_TEAM_UNASSIGNED;
		m_voiceBank = 0;
		m_prefersSilencer = false; // FF_TODO: Review if this concept applies to FF
		for(int i=0; i<MAX_WEAPON_PREFS; ++i) m_weaponPreference[i] = FF_WEAPON_NONE;
	}

	~BotProfile( void )
	{
		if ( m_name )
			delete [] m_name;
	}

	const char *GetName( void ) const					{ return m_name; }
	float GetAggression( void ) const					{ return m_aggression; }
	float GetSkill( void ) const						{ return m_skill; }
	float GetTeamwork( void ) const						{ return m_teamwork; }

	FFWeaponID GetWeaponPreference( int i ) const		{ return m_weaponPreference[ i ]; }
	const char *GetWeaponPreferenceAsString( int i ) const; // Implementation will be FF_TODO in .cpp
	int GetWeaponPreferenceCount( void ) const			{ return m_weaponPreferenceCount; }
	bool HasPrimaryPreference( void ) const;			// Implementation will be FF_TODO in .cpp
	bool HasPistolPreference( void ) const;				// Implementation will be FF_TODO in .cpp

	int GetCost( void ) const							{ return m_cost; }
	int GetSkin( void ) const							{ return m_skin; } // Assumed to be class slot ID for FF
	bool IsDifficulty( BotDifficultyType diff ) const;
	int GetVoicePitch( void ) const						{ return m_voicePitch; }
	float GetReactionTime( void ) const					{ return m_reactionTime; }
	float GetAttackDelay( void ) const					{ return m_attackDelay; }
	int GetVoiceBank() const							{ return m_voiceBank; }

	bool IsValidForTeam( int team ) const; // team will be FF_TEAM_*

	bool PrefersSilencer() const						{ return m_prefersSilencer; } // FF_TODO: Review for FF

	bool InheritsFrom( const char *name ) const;

private:	
	friend class BotProfileManager;

	void Inherit( const BotProfile *parent, const BotProfile *baseline );

	char *m_name;
	float m_aggression;
	float m_skill;
	float m_teamwork;

	enum { MAX_WEAPON_PREFS = 16 };
	FFWeaponID m_weaponPreference[ MAX_WEAPON_PREFS ]; // Changed to FFWeaponID
	int m_weaponPreferenceCount;

	int m_cost;
	int m_skin;
	unsigned char m_difficultyFlags;
	int m_voicePitch;
	float m_reactionTime;
	float m_attackDelay;
	int m_teams;

	bool m_prefersSilencer;

	int m_voiceBank;

	CUtlVector< const BotProfile * > m_templates;
};
typedef CUtlLinkedList<BotProfile *> BotProfileList;


inline bool BotProfile::IsDifficulty( BotDifficultyType diff ) const
{
	return (m_difficultyFlags & (1 << diff)) ? true : false;
}

inline void BotProfile::Inherit( const BotProfile *parent, const BotProfile *baseline )
{
	if (parent->m_aggression != baseline->m_aggression) m_aggression = parent->m_aggression;
	if (parent->m_skill != baseline->m_skill) m_skill = parent->m_skill;
	if (parent->m_teamwork != baseline->m_teamwork) m_teamwork = parent->m_teamwork;

	if (parent->m_weaponPreferenceCount != baseline->m_weaponPreferenceCount)
	{
		m_weaponPreferenceCount = parent->m_weaponPreferenceCount;
		for( int i=0; i<parent->m_weaponPreferenceCount; ++i )
			m_weaponPreference[i] = parent->m_weaponPreference[i];
	}
	if (parent->m_cost != baseline->m_cost) m_cost = parent->m_cost;
	if (parent->m_skin != baseline->m_skin) m_skin = parent->m_skin;
	if (parent->m_difficultyFlags != baseline->m_difficultyFlags) m_difficultyFlags = parent->m_difficultyFlags;
	if (parent->m_voicePitch != baseline->m_voicePitch) m_voicePitch = parent->m_voicePitch;
	if (parent->m_reactionTime != baseline->m_reactionTime) m_reactionTime = parent->m_reactionTime;
	if (parent->m_attackDelay != baseline->m_attackDelay) m_attackDelay = parent->m_attackDelay;
	if (parent->m_teams != baseline->m_teams) m_teams = parent->m_teams;
	if (parent->m_voiceBank != baseline->m_voiceBank) m_voiceBank = parent->m_voiceBank;
	if (parent->m_prefersSilencer != baseline->m_prefersSilencer) m_prefersSilencer = parent->m_prefersSilencer;
	m_templates.AddToTail( parent );
}

//--------------------------------------------------------------------------------------------------------------
class BotProfileManager
{
public:
	BotProfileManager( void );
	~BotProfileManager( void );

	void Init( const char *filename, unsigned int *checksum = NULL );
	void Reset( void );

	const BotProfile *GetProfile( const char *name, int team ) const;
	const BotProfile *GetProfileMatchingTemplate( const char *profileName, int team, BotDifficultyType difficulty ) const;
	const BotProfileList *GetProfileList( void ) const		{ return &m_profileList; }
	const BotProfile *GetRandomProfile( BotDifficultyType difficulty, int team, FFWeaponID weaponType ) const; // weaponType changed to FFWeaponID

	const char * GetCustomSkin( int index );
	const char * GetCustomSkinModelname( int index );
	const char * GetCustomSkinFname( int index );
	int GetCustomSkinIndex( const char *name, const char *filename = NULL );

	typedef CUtlVector<char *> VoiceBankList;
	const VoiceBankList *GetVoiceBanks( void ) const		{ return &m_voiceBanks; }
	int FindVoiceBankIndex( const char *filename );

protected:
	BotProfileList m_profileList;
	BotProfileList m_templateList;

	VoiceBankList m_voiceBanks;

	char *m_skins[ NumCustomSkins ];
	char *m_skinModelnames[ NumCustomSkins ];
	char *m_skinFilenames[ NumCustomSkins ];
	int m_nextSkin;
};

extern BotProfileManager *TheBotProfiles;

#endif // _BOT_PROFILE_H_
