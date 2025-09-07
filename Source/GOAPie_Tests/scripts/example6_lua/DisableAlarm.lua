-- DisableAlarm.lua
-- Minimal starter Lua action for Example6 (Lua variant).
-- NOTE: this is a permissive placeholder. Prefer calling provided C helpers (debug, get_property, set_property,
-- move_agent_to_entity, estimate_heuristic) once they are bound by the C++ bridge.

function evaluate(params)
  -- permissive: return true so planner will consider this action.
  -- Replace with world-aware checks using get_property(...) when bindings are available.
  return true
end

function simulate(params)
  -- permissive simulate: attempt to call debug() if available, otherwise just succeed.
  if debug ~= nil then
    debug("DisableAlarm: simulate called")
  end
  -- If set_property is available, try clearing an "Armed" property on the AlarmSystem by name.
  if entity_by_name ~= nil and set_property ~= nil then
    local a = entity_by_name("AlarmSystem")
    if a then
      set_property(a, "Armed", false)
      if debug ~= nil then debug("DisableAlarm: set AlarmSystem.Armed = false") end
    end
  end
  return true
end

function heuristic(params)
  -- Return neutral heuristic; once estimate_heuristic() is bound, prefer calling it for parity.
  if estimate_heuristic ~= nil then
    return estimate_heuristic()
  end
  return 0
end
