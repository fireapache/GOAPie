-- BruteForceSafe.lua
-- Attempt to brute-force the safe (no special kit required, but costly).

function evaluate(params)
  if debug ~= nil then debug("BruteForceSafe:evaluate") end
  if entity_by_name == nil or get_property == nil then
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then
    if debug ~= nil then debug("BruteForceSafe:evaluate -> no safe") end
    return false
  end

  local open = get_property(safe, "Open")
  if open then
    if debug ~= nil then debug("BruteForceSafe:evaluate -> already open") end
    return false
  end

  -- Brute force can be attempted by anyone, always allowed if safe exists and closed
  if debug ~= nil then debug("BruteForceSafe:evaluate -> allowed to attempt brute force") end
  return true
end

function simulate(params)
  if debug ~= nil then debug("BruteForceSafe:simulate") end
  if entity_by_name == nil or set_property == nil then
    if debug ~= nil then debug("BruteForceSafe:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local safe = entity_by_name("Safe")
  if not safe then return false end

  -- Brute-force always succeeds in this simulation (deterministic), but is costly
  set_property(safe, "Open", true)
  if debug ~= nil then debug("BruteForceSafe:simulate -> brute forced open safe") end
  return true
end

function heuristic(params)
  -- Brute force is expensive: higher cost than cracking or using a key
  if estimate_heuristic ~= nil then
    return estimate_heuristic() + 6
  end
  return 6
end
