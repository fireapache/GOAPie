-- OpenSafeWithCode.lua
-- Use a found code/item to open the safe.

function evaluate(params)
  if debug ~= nil then debug("OpenSafeWithCode:evaluate") end
  if entity_by_name == nil or get_property == nil then
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then
    if debug ~= nil then debug("OpenSafeWithCode:evaluate -> no safe") end
    return false
  end

  local open = get_property(safe, "Open")
  if open then
    if debug ~= nil then debug("OpenSafeWithCode:evaluate -> already open") end
    return false
  end

  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end
  if ag and get_property ~= nil then
    local hasItem = get_property(ag, "HasItem")
    if hasItem then
      if debug ~= nil then debug("OpenSafeWithCode:evaluate -> agent has code/item") end
      return true
    end
  end

  if debug ~= nil then debug("OpenSafeWithCode:evaluate -> no code") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("OpenSafeWithCode:simulate") end
  if entity_by_name == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("OpenSafeWithCode:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then return false end

  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end

  if ag then
    local hasItem = get_property(ag, "HasItem")
    if hasItem then
      set_property(safe, "Open", true)
      set_property(ag, "HasItem", false)
      if debug ~= nil then debug("OpenSafeWithCode:simulate -> opened safe with code/item") end
      return true
    end
  end

  if debug ~= nil then debug("OpenSafeWithCode:simulate -> failed to open") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
