#ifndef FF_BOT_UTIL_H
#define FF_BOT_UTIL_H

#pragma once

#include "cbase.h" // For basic types, possibly UTIL_LogPrintf or similar if used by PrintIfWatched

// Forward declaration
class CFFBot;
class CBasePlayer; // For UTIL_PlayerByIndex, UTIL_HumansOnTeam etc.
class BotProfile;  // For UTIL_ConstructBotNetName

// Example utility function declaration (implementation would go in a .cpp or be inline)
// For actual printing, it might need access to bot's player index or other means to filter messages.
inline void BotLog(CFFBot *bot, const char *fmt, ...)
{
    // Basic stub for logging, replace with actual engine logging if available and appropriate
    // For example, using DevMsg or a bot-specific logging channel.
#ifdef _DEBUG
    va_list argptr;
    char string[1024];
    va_start (argptr,fmt);
    Q_vsnprintf(string, sizeof(string), fmt,argptr);
    va_end (argptr);

    // Could use Msg, DevMsg, or a bot specific log here.
    // For now, just a placeholder.
    // Msg("[Bot %d] %s", bot ? bot->entindex() : 0, string);
#endif
}

// Placeholder for PrintIfWatched - actual implementation needs bot->IsWatchedByLocalPlayer()
// which might be complex to recreate minimally.
inline void PrintIfWatched(CFFBot *bot, const char *fmt, ...)
{
    // TODO_FF: Implement actual logic if this function is to be used.
    // This would check if the bot is being watched by the local player (dev feature).
    // For now, it's a stub.
    // if (bot && bot->IsWatchedByLocalPlayer()) { /* format and print using BotLog or similar */ }
}


// Utility functions that were previously in cs_bot_manager.cpp or similar,
// now potentially part of a general bot utility header.

// Example:
inline int UTIL_HumansOnTeam( int teamNum, bool ignoreSpectators = false )
{
    int count = 0;
    for ( int i = 1; i <= gpGlobals->maxClients; i++ )
    {
        CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
        if ( !pPlayer || pPlayer->IsFakeClient() ) // IsFakeClient() or IsBot()
            continue;
        if ( ignoreSpectators && pPlayer->GetTeamNumber() == 0 /* TEAM_SPECTATOR */) // Assuming 0 is spectator
            continue;
        if ( teamNum == 0 /* TEAM_ANY */ || pPlayer->GetTeamNumber() == teamNum )
        {
            count++;
        }
    }
    return count;
}

inline int UTIL_HumansInGame( bool ignoreSpectators = false )
{
    return UTIL_HumansOnTeam( 0 /* TEAM_ANY */, ignoreSpectators );
}


// From bot_profile.cpp in CS:S (or similar)
// TODO_FF: Ensure BotProfile has GetName() and GetDifficulty() or similar for this to work.
// BotDifficultyName needs to be defined (likely in bot_constants.h or bot_profile.h itself).
// MAX_PLAYER_NAME_LENGTH needs to be defined.
/*
inline void UTIL_ConstructBotNetName( char *name, int nameLen, const BotProfile *profile )
{
    if (!profile || !name)
        return;

    const char *difficulty = "";
    if (profile->GetDifficulty() >= 0 && profile->GetDifficulty() < NUM_DIFFICULTY_LEVELS)
    {
        difficulty = BotDifficultyName[profile->GetDifficulty()];
    }

    // TODO_FF: Bot name prefixing might need different logic for FF.
    // This is a simplified version. Original CS had <weaponclass> etc.
    if (cv_bot_prefix.GetString() && *cv_bot_prefix.GetString())
    {
        char temp[MAX_PLAYER_NAME_LENGTH];
        Q_strncpy(temp, cv_bot_prefix.GetString(), sizeof(temp));

        // Replace <difficulty>
        char finalPrefix[MAX_PLAYER_NAME_LENGTH];
        UTIL_ReplaceSubstring( temp, "<difficulty>", difficulty, finalPrefix, sizeof(finalPrefix) );

        Q_snprintf( name, nameLen, "%s %s", finalPrefix, profile->GetName() );
    }
    else
    {
        Q_snprintf( name, nameLen, "%s", profile->GetName() );
    }
}
*/


// Add other utility function declarations as needed.
// For example, string manipulation, pathfinding helpers not tied to CFFBot class, etc.

#endif // FF_BOT_UTIL_H
