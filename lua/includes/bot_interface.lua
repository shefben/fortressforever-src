-- bot_interface.lua
-- This file provides functions for C++ bots to query Lua-managed game state.

-----------------------------------------------------------------------------
-- Control Point State Polling Function
-----------------------------------------------------------------------------
function FF_GetControlPointStates()
    local cpStates = {}

    if not command_points or type(command_points) ~= "table" then
        -- command_points table doesn't exist or isn't a table, return empty
        -- This might happen if called before base_cp_default.lua (or map lua) populates it.
        if _scriptman and _scriptman.LuaWarning then
             _scriptman:LuaWarning("FF_GetControlPointStates: 'command_points' global table not found or not a table.")
        else
             print("[LUA WARNING] FF_GetControlPointStates: 'command_points' global table not found or not a table.")
        end
        return cpStates
    end

    for cp_number_lua, cp_data in ipairs(command_points) do -- ipairs for numerically indexed array part
        if cp_data and type(cp_data) == "table" then
            local state_entry = {}

            -- pointID in C++ is expected to be 0-indexed, matching m_ControlPoints array index.
            -- Lua's ipairs gives 1-indexed cp_number_lua.
            state_entry.pointID = (cp_data.cp_number or cp_number_lua) - 1

            -- Owning Team
            state_entry.owningTeam = cp_data.defending_team or Team.kUnassigned -- Default to unassigned

            -- Capture Progress
            -- C++ FFGameState::FF_ControlPointState::captureProgress is float[MAX_TEAMS_FF]
            -- Assuming MAX_TEAMS_FF in C++ is 4, and team IDs are 0 (None/Spec), 1 (Red), 2 (Blue), 3 (Yellow/Green)
            -- Lua's team IDs (Team.kRed, etc.) are typically 1, 2, 3, 4.
            -- The C++ array might be indexed 0..3 corresponding to these.
            -- Need to be careful with team ID mapping if C++ uses 0-3 for actual teams.
            -- For now, assume C++ progress array is indexed directly by Lua team IDs (1-4),
            -- and C++ side will handle mapping if its internal MAX_TEAMS_FF is smaller or uses 0-indexing for teams.
            -- Or, more robustly, map specific known team IDs.
            state_entry.captureProgress = {}
            local luaTeamIdsToPoll = { Team.kRed, Team.kBlue, Team.kYellow, Team.kGreen } -- explicit list
            local cppTeamIndex = 0 -- For direct mapping to a 0-indexed C++ array up to MAX_TEAMS_FF

            if cp_data.cap_status and type(cp_data.cap_status) == "table" then
                for _, team_id_lua in ipairs(luaTeamIdsToPoll) do
                    if cppTeamIndex < 4 then -- Assuming C++ MAX_TEAMS_FF is at least 4 for this direct mapping
                        local progress = cp_data.cap_status[team_id_lua] or 0.0
                        -- Store progress using the Lua team_id as key, C++ side will map if necessary
                        -- Or, if C++ array is 0-3 for Red,Blue,Yellow,Green: state_entry.captureProgress[cppTeamIndex+1] (1-indexed for Lua table)
                        state_entry.captureProgress[team_id_lua] = progress
                        cppTeamIndex = cppTeamIndex + 1
                    end
                end
            else
                -- Default progress if cap_status is not available
                for _, team_id_lua in ipairs(luaTeamIdsToPoll) do
                     if cppTeamIndex < 4 then
                        state_entry.captureProgress[team_id_lua] = 0.0
                        cppTeamIndex = cppTeamIndex + 1
                    end
                end
            end

            -- Locked Status
            -- base_cp.lua uses cp.next_cap_zone_timer[team_id] > 0 to make a point temporarily uncapturable by that team.
            -- A general 'isLocked' (by map logic, not temporary timer) isn't a direct property in cp_data.
            -- This would typically be a property of the CP entity itself if it's permanently locked by map design.
            -- For C++ polling, we might need to check if *any* team's next_cap_zone_timer > 0 to consider it "contested/recently_capped_locked".
            -- Or, assume CPs are generally not "map-locked" unless specifically set by an entity property.
            -- For now, sending a default 'false'. C++ side can refine this based on its needs.
            state_entry.isLocked = false
            if cp_data.entity and cp_data.entity.IsLocked then -- Hypothetical: if CP entity has an IsLocked method
                 state_entry.isLocked = cp_data.entity:IsLocked()
            end
            -- TODO_FF: Determine how to get actual map-logic locked status.
            --          Could also check if ALL next_cap_zone_timer > 0 for all enemy teams.

            table.insert(cpStates, state_entry)
        else
            -- Insert a nil or an empty table if cp_data is not as expected for this index
            -- This ensures the C++ side receives an array of the same length as command_points
            table.insert(cpStates, {}) -- Insert empty table to avoid C++ erroring on nil, C++ side should check pointID
            if _scriptman and _scriptman.LuaWarning then
                 _scriptman:LuaWarning("FF_GetControlPointStates: Invalid or missing cp_data at Lua index " .. tostring(cp_number_lua))
            else
                 print("[LUA WARNING] FF_GetControlPointStates: Invalid or missing cp_data at Lua index " .. tostring(cp_number_lua))
            end
        end
    end

    return cpStates
end

-- To make this function available, this file should be included in a relevant Lua script
-- that is loaded by the game, for example, at the top of 'lua/includes/base_cp.lua':
-- IncludeScript("bot_interface")
-- or in the map's main Lua file.

DevMsg( "[LUA] bot_interface.lua loaded.\n" )
