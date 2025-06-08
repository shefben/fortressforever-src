#ifndef FF_BOT_CONSTANTS_H
#define FF_BOT_CONSTANTS_H

#pragma once

// Forward declarations (if any standard types are used in enums and not included yet)

// Basic Difficulty Levels (assuming these were in the original)
enum BotDifficultyType
{
    BOT_EASY,
    BOT_NORMAL,
    BOT_HARD,
    BOT_EXPERT,
    NUM_DIFFICULTY_LEVELS // Keep if used as array size
};

// Basic Team Definitions (Fortress Forever specific)
// These are expected to be defined in the main game's headers (e.g., ff_shareddefs.h or similar)
// Example: TEAM_RED, TEAM_BLUE, TEAM_SPECTATOR, TEAM_UNASSIGNED
// The bot code will use these game-defined team enums.

// Task Types (ensure this is comprehensive for existing bot logic)
// Prefix with BOT_TASK_ to avoid potential conflicts with other enums.
enum BotTaskType
{
    BOT_TASK_SEEK_AND_DESTROY,

    // TODO_FF: Review and adapt CS-specific tasks for FF objectives or remove
    BOT_TASK_PLANT_BOMB,          // CS Specific: Example: Secure Intel, Sabotage Objective
    BOT_TASK_FIND_TICKING_BOMB,   // CS Specific: Example: Locate Dropped Intel, Find Active Detonator
    BOT_TASK_DEFUSE_BOMB,         // CS Specific: Example: Retrieve Intel, Disarm Detonator
    BOT_TASK_GUARD_TICKING_BOMB,  // CS Specific: Example: Guard Secured Intel, Guard Active Detonator
    BOT_TASK_GUARD_BOMB_DEFUSER,  // CS Specific: Example: Guard Teammate Securing Intel
    BOT_TASK_GUARD_LOOSE_BOMB,    // CS Specific: Example: Guard Dropped Intel
    BOT_TASK_GUARD_BOMB_ZONE,     // CS Specific: Example: Guard Objective Area
    BOT_TASK_ESCAPE_FROM_BOMB,    // CS Specific: Example: Retreat From Imminent Danger

    BOT_TASK_GUARD_INITIAL_ENCOUNTER,
    BOT_TASK_HOLD_POSITION,
    BOT_TASK_FOLLOW,

    // TODO_FF: VIP mode tasks, adapt if FF has a VIP mode
    BOT_TASK_VIP_ESCAPE,          // CS Specific
    BOT_TASK_GUARD_VIP_ESCAPE_ZONE, // CS Specific

    // TODO_FF: Hostage rescue tasks, remove if FF has no hostages
    BOT_TASK_COLLECT_HOSTAGES,    // CS Specific
    BOT_TASK_RESCUE_HOSTAGES,     // CS Specific
    BOT_TASK_GUARD_HOSTAGES,      // CS Specific
    BOT_TASK_GUARD_HOSTAGE_RESCUE_ZONE, // CS Specific

    BOT_TASK_MOVE_TO_LAST_KNOWN_ENEMY_POSITION,
    BOT_TASK_MOVE_TO_SNIPER_SPOT,
    BOT_TASK_SNIPING,

    // Add FF-specific tasks here:
    BOT_TASK_CAPTURE_FLAG,        // Example for CTF
    BOT_TASK_RETURN_FLAG,         // Example for CTF
    BOT_TASK_DEFEND_FLAG_STAND,   // Example for CTF
    BOT_TASK_CAPTURE_POINT,       // Example for CP
    BOT_TASK_DEFEND_POINT,        // Example for CP
    // BOT_TASK_ESCORT_VIP_FF,    // Example for FF VIP (if different from CS)
    // BOT_TASK_ASSASSINATE_VIP_FF, // Example for FF VIP (if different from CS)

    NUM_BOT_TASKS // Must be last
};

// Aiming Priorities
enum PriorityType
{
    PRIORITY_LOWEST, // Added for completeness
    PRIORITY_LOW,
    PRIORITY_MEDIUM,
    PRIORITY_HIGH,
    PRIORITY_UNINTERRUPTABLE
};

// Route Types
enum RouteType
{
    SAFEST_ROUTE,
    FASTEST_ROUTE
};

// Navigation-related constants (if not defined in nav_mesh or similar)
// Example:
// const float HalfHumanWidth = 16.0f; // This might be game-specific rather than bot-specific
// const float StepHeight = 18.0f;     // Typically game-specific
// const float JumpHeight = 56.0f;     // Typically game-specific

// Other bot-specific constants
// Example:
// const int MAX_BOT_ARG_LENGTH = 128;

// TODO_FF: Add any other enums or constants that were used by the CS bot code
// and are not defined elsewhere in FF's headers or more appropriate shared locations.
// For example, weapon class types if the original bot profile used them,
// but CFFWeaponInfo and ff_bot_weapon_id.h/cpp now handle this for FF.
// Radio event enums (RADIO_AFFIRMATIVE, etc.) should also be here if not defined globally for the game.

#endif // FF_BOT_CONSTANTS_H
