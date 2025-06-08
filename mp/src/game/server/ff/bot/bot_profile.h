#ifndef FF_BOT_PROFILE_H
#define FF_BOT_PROFILE_H

#pragma once

// Minimal BotProfile class shell based on observed usage
// TODO_FF: This is a minimal recreation. Original CS bot_profile.h had extensive attributes.
// FF bots may need a more detailed profile system later.

// Forward declare enums used by BotProfile if their definitions are in other headers not included here.
// enum BotDifficultyType; // Assuming BotDifficultyType is in bot_constants.h which might be included by users of BotProfile
// enum FFWeaponID;      // Assuming FFWeaponID is in ff_weapon_base.h which might be included by users of BotProfile

class BotProfile
{
public:
    BotProfile() :
        m_skill(0.5f),
        m_attackDelay(0.0f),
        m_teamwork(0.5f),
        m_voiceBankIndex(0), // Default voice bank
        m_skin(0)            // Default skin/class appearance
        // m_difficulty = BOT_NORMAL; // Example if difficulty is stored here
    {}

    // Methods inferred from usage in CFFBot code
    float GetSkill() const { return m_skill; }
    float GetAttackDelay() const { return m_attackDelay; } // AKA ReactionTime in some contexts
    float GetTeamwork() const { return m_teamwork; }
    bool PrefersSilencer() const { return false; } // Assuming false for FF for now
    int GetVoiceBank() const { return m_voiceBankIndex; } // Example: used by CFFBot::GetChatter()
    int GetVoicePitch() const { return 100; } // Example: used by CFFBot::SpeakAudio()
    int GetSkin() const { return m_skin; } // Example: used by CFFBot::Initialize() for class selection


    // TODO_FF: Add methods for weapon preferences if bots are to have individual loadouts beyond class defaults.
    // These were present in CSBotProfile.
    virtual int GetWeaponPreferenceCount() const { return 0; }
    virtual int GetWeaponPreference(int i) const { return 0; } // returning FFWeaponID
    virtual const char *GetWeaponPreferenceAsString( int i ) const { return ""; }
    virtual bool HasPrimaryPreference() const { return GetWeaponPreferenceCount() > 0; } // Simplified
    virtual bool HasPistolPreference() const { return false; } // Simplified


    // Example if difficulty is part of the profile directly
    // BotDifficultyType GetDifficulty() const { return m_difficulty; }
    // void SetDifficulty(BotDifficultyType diff) { m_difficulty = diff; }

    // Other methods that were on CSBotProfile that might be needed
    const char *GetName() const { return m_name.IsEmpty() ? "Bot" : m_name.Get(); }
    void SetName(const char *name) { m_name = name; }
    bool IsDifficulty( /* BotDifficultyType difficulty */ int difficulty ) const { return true; /* Placeholder */ } // Needs BotDifficultyType
    bool InheritsFrom( const char *templateName ) const { return false; /* Placeholder */ }


private:
    float m_skill;
    float m_attackDelay;
    float m_teamwork;
    int m_voiceBankIndex;
    int m_skin;
    // BotDifficultyType m_difficulty; // Example
    CUtlString m_name; // Example for name storage

    // TODO_FF: Add other members to store profile attributes like weapon preferences, cost ratio, etc.
};

// Global manager for profiles (similar to TheBotProfiles in CS)
// This would be defined and implemented in ff_bot_manager.cpp or a new ff_bot_profile.cpp
class BotProfileManager
{
public:
    // TODO_FF: Implement methods to load, get, and manage BotProfile instances.
    // static const BotProfile *GetProfile(const char *name, int team = 0);
    // static const BotProfile *GetRandomProfile(BotDifficultyType difficulty, int team = 0, CSWeaponType weaponType = WEAPONTYPE_UNKNOWN);
    // static const CUtlVector<char *> *GetVoiceBanks( void );
    // static int FindVoiceBankIndex( const char *filename );
    // static const CCSWeaponInfo &GetWeaponInfo( int weaponID ); // This might belong to a weapon data manager
};

// extern BotProfileManager *TheBotProfiles; // Declaration for global instance

#endif // FF_BOT_PROFILE_H
