-- Lockpick.lua
-- Placeholder Lua action: attempt to pick a lock on a connector or door.

function evaluate(params)
  if debug ~= nil then debug("Lockpick:evaluate") end
  if tag_set == nil or get_property == nil then
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then
    if debug ~= nil then debug("Lockpick:evaluate -> no connectors") end
    return false
  end

  for i=1,#connectors do
    local g = connectors[i]
    local locked = get_property(g, "Locked")
    local pickable = get_property(g, "Pickable")
    if locked and pickable then
      if debug ~= nil then debug("Lockpick:evaluate -> pickable locked connector found") end
      return true
    end
  end

  if debug ~= nil then debug("Lockpick:evaluate -> nothing pickable") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("Lockpick:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("Lockpick:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then return false end

  for i=1,#connectors do
    local g = connectors[i]
    local locked = get_property(g, "Locked")
    local pickable = get_property(g, "Pickable")
    if locked and pickable then
      -- Simulate success probabilistically by always succeeding in placeholder
      set_property(g, "Locked", false)
      if debug ~= nil then debug("Lockpick:simulate -> unlocked connector") end
      return true
    end
  end

  if debug ~= nil then debug("Lockpick:simulate -> failed") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
