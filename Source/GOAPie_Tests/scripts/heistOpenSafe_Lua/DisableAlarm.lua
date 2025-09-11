-- DisableAlarm.lua
-- Minimal starter Lua action chunk for example6.
-- Exposes: evaluate(params), simulate(params), heuristic(params)
-- Uses helper functions injected into the chunk env: debug(), get_property(guid, name), set_property(guid, name, value), entity_by_name(name)

local function safe_tostring(v)
    if v == nil then return "nil" end
    return tostring(v)
end

function evaluate(params)
    -- Return true if the alarm is currently enabled (so action is applicable).
    -- If property is absent, be permissive and allow the planner to consider the action.
    local alarmGuid = entity_by_name("Alarm")
    if not alarmGuid then
        debug("DisableAlarm.evaluate: no Alarm entity found; permissive=true")
        return true
    end

    local enabled = get_property(alarmGuid, "Enabled")
    debug("DisableAlarm.evaluate: Alarm Enabled = " .. safe_tostring(enabled))
    if enabled == nil then
        return true
    end

    return enabled == true
end

function simulate(params)
    -- Attempt to disable the alarm by setting its Enabled property to false.
    local alarmGuid = entity_by_name("Alarm")
    if not alarmGuid then
        debug("DisableAlarm.simulate: no Alarm entity found -> fail")
        return false
    end

    local ok = set_property(alarmGuid, "Enabled", false)
    debug("DisableAlarm.simulate: set Enabled=false -> " .. safe_tostring(ok))
    return ok == true
end

function heuristic(params)
    -- Simple heuristic: zero cost for this action.
    return 0
end
