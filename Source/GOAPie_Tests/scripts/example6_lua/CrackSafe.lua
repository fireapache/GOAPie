-- CrackSafe.lua
-- Attempt to crack the safe using specialized cracking kit.

function evaluate(params)
  if debug ~= nil then debug("CrackSafe:evaluate") end
  if entity_by_name == nil or get_property == nil then
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then
    if debug ~= nil then debug("CrackSafe:evaluate -> no safe") end
    return false
  end

  local open = get_property(safe, "Open")
  if open then
    if debug ~= nil then debug("CrackSafe:evaluate -> already open") end
    return false
  end

  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end
  if ag and get_property ~= nil then
    -- Require a cracking kit to attempt cracking
    local hasKit = get_property(ag, "HasCrackKit")
    if hasKit then
      if debug ~= nil then debug("CrackSafe:evaluate -> agent has crack kit") end
      return true
    end
  end

  if debug ~= nil then debug("CrackSafe:evaluate -> cannot crack safe") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("CrackSafe:simulate") end
  if entity_by_name == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("CrackSafe:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then return false end

  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end

  -- Use kit to open safe in simulation (deterministic for planner)
  if ag then
    local hasKit = get_property(ag, "HasCrackKit")
    if hasKit then
      set_property(safe, "Open", true)
      -- consume the kit
      set_property(ag, "HasCrackKit", false)
      if debug ~= nil then debug("CrackSafe:simulate -> cracked open safe") end
      return true
    end
  end

  if debug ~= nil then debug("CrackSafe:simulate -> failed to crack") end
  return false
end

function heuristic(params)
  -- Prefer the generic estimate if available, otherwise return modest cost
  if estimate_heuristic ~= nil then
    -- cracking is relatively costly but better than brute force
    return estimate_heuristic() + 2
  end
  return 2
end
