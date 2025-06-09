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
    BOT_TASK_CARRY_FLAG_TO_CAP,   // New task for carrying flag to capture point
    BOT_TASK_CAPTURE_POINT,       // Example for CP
    BOT_TASK_DEFEND_POINT,        // Example for CP
    BOT_TASK_ESCORT_VIP_FF,       // Example for FF VIP (if different from CS) - uncomment if used
    BOT_TASK_ASSASSINATE_VIP_FF,  // Example for FF VIP (if different from CS) - uncomment if used
    BOT_TASK_VIP_ESCAPE_FF,       // Example for FF VIP (already used in IdleState)

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

// --- Weapon Scoring Constants for EquipBestWeapon ---
const float SCORE_BASE = 100.0f;
const float SCORE_CLASS_WEAPON_BONUS = 500.0f;
const float SCORE_AMMO_FULL_CLIP_BONUS = 50.0f;
const float SCORE_AMMO_LOW_PENALTY_FACTOR = -200.0f; // Multiplied by (1 - ammo_ratio)
const float SCORE_NO_RESERVE_AMMO_PENALTY = -10000.0f;
const float SCORE_MELEE_CLOSE_BONUS = 300.0f;
const float SCORE_SHOTGUN_CLOSE_BONUS = 400.0f;
const float SCORE_SNIPER_AT_RANGE_BONUS = 450.0f;
const float SCORE_SNIPER_VERY_FAR_BONUS = 300.0f;
const float SCORE_SNIPER_TOO_CLOSE_PENALTY = -8000.0f;
const float SCORE_ROCKET_OPTIMAL_RANGE_BONUS = 350.0f;
const float SCORE_ROCKET_TOO_CLOSE_PENALTY = -7000.0f;
const float SCORE_GENERIC_OUT_OF_RANGE_PENALTY = -500.0f;
const float SCORE_TASK_SNIPING_BONUS = 10000.0f;
// Task-specific bonuses/penalties
const float SCORE_FLAG_CARRIER_BONUS = 200.0f;          // Bonus for good flag carrying weapons (shotgun, nailgun)
const float SCORE_FLAG_CARRIER_PENALTY = -300.0f;       // Penalty for bad flag carrying weapons (sniper)
const float SCORE_DEFENSE_BONUS = 250.0f;               // Bonus for defensive weapons (rockets, pipes, AC)
const float SCORE_DEFENSE_MELEE_PENALTY = -400.0f;      // Penalty for melee on defense
const float SCORE_ASSASSIN_BONUS = 300.0f;              // Bonus for good assassination weapons
const float SCORE_BODYGUARD_BONUS = 200.0f;             // Bonus for good bodyguard weapons
// Special class item bonuses (base values, can be modified by context)
const float SCORE_MEDKIT_BASE_BONUS = 50.0f;
const float SCORE_SPANNER_BASE_BONUS = 50.0f;
const float SCORE_RAILGUN_ENGINEER_COMBAT_BONUS = 100.0f;


// --- Distance Thresholds for Weapon Scoring ---
const float DIST_MELEE_MAX_EFFECTIVE_RANGE_FACTOR = 1.5f; // Multiplier for weaponInfo.m_flRange
const float DIST_CLOSE_COMBAT = 400.0f;
const float DIST_SNIPER_MIN = 250.0f;
const float DIST_SNIPER_OPTIMAL_MIN = 1000.0f;
const float DIST_SNIPER_OPTIMAL_MAX = 4000.0f;
const float DIST_ROCKET_MIN = 200.0f; // Min safety distance for splash
const float DIST_ROCKET_OPTIMAL_MAX = 2000.0f;
const float DIST_GENERIC_EFFECTIVE_RANGE_FACTOR = 1.2f; // Multiplier for weaponInfo.m_flRange


// Team definition for VIP mode (Hunted)
// Ensure TEAM_ID_BLUE is defined in an included header (e.g. ff_shareddefs.h or similar)
// For example, if TEAM_BLUE is 2 in the game's enum: #define FF_HUNTED_TEAM 2
// Using the placeholder from ff_gamestate.h style:
#define FF_HUNTED_TEAM TEAM_ID_BLUE // Placeholder, ensure TEAM_ID_BLUE is correctly defined and accessible

#endif // FF_BOT_CONSTANTS_H
