-- OpenSafeWithKey.lua
-- Use a key to open the safe.

function evaluate(params)
  if debug ~= nil then debug("OpenSafeWithKey:evaluate") end
  if entity_by_name == nil or get_property == nil then
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then
    if debug ~= nil then debug("OpenSafeWithKey:evaluate -> no safe") end
    return false
  end

  local open = get_property(safe, "Open")
  if open then
    if debug ~= nil then debug("OpenSafeWithKey:evaluate -> already open") end
    return false
  end

  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end
  if ag and get_property ~= nil then
    local hasKey = get_property(ag, "HasKey")
    if hasKey then
      if debug ~= nil then debug("OpenSafeWithKey:evaluate -> agent has key") end
      return true
    end
  end

  if debug ~= nil then debug("OpenSafeWithKey:evaluate -> cannot open with key") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("OpenSafeWithKey:simulate") end
  if entity_by_name == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("OpenSafeWithKey:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then return false end

  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end

  -- Consume key and open safe in simulation
  if ag then
    local hasKey = get_property(ag, "HasKey")
    if hasKey then
      set_property(safe, "Open", true)
      set_property(ag, "HasKey", false)
      if debug ~= nil then debug("OpenSafeWithKey:simulate -> opened safe with key") end
      return true
    end
  end

  if debug ~= nil then debug("OpenSafeWithKey:simulate -> failed to open") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
