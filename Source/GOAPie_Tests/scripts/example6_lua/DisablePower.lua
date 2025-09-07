-- DisablePower.lua
-- Placeholder Lua action: disable power to area/equipment.

function evaluate(params)
  if debug ~= nil then debug("DisablePower:evaluate") end
  -- permissive if helpers missing
  if entity_by_name == nil or tag_set == nil or get_property == nil then
    return true
  end

  local powerNodes = tag_set("PowerNode")
  if not powerNodes then
    if debug ~= nil then debug("DisablePower:evaluate -> no power nodes") end
    return false
  end

  -- If any node is enabled, allow disabling
  for i=1,#powerNodes do
    local g = powerNodes[i]
    local enabled = get_property(g, "Enabled")
    if enabled then
      if debug ~= nil then debug("DisablePower:evaluate -> node enabled") end
      return true
    end
  end

  if debug ~= nil then debug("DisablePower:evaluate -> nothing to do") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("DisablePower:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("DisablePower:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local powerNodes = tag_set("PowerNode")
  if not powerNodes then return false end

  -- Disable the first enabled node we find (simulation only)
  for i=1,#powerNodes do
    local g = powerNodes[i]
    local enabled = get_property(g, "Enabled")
    if enabled then
      set_property(g, "Enabled", false)
      if debug ~= nil then debug("DisablePower:simulate -> disabled node") end
      return true
    end
  end

  if debug ~= nil then debug("DisablePower:simulate -> none disabled") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
