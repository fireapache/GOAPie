-- MoveInside.lua
-- Placeholder Lua action: move agent into the building/interior after entry.

function evaluate(params)
  if debug ~= nil then debug("MoveInside:evaluate") end
  if agent_guid == nil or get_property == nil then
    return true
  end

  local ag = agent_guid()
  if not ag then return true end

  local cur = get_property(ag, "CurrentRoom")
  if cur and cur ~= 0 then
    if debug ~= nil then debug("MoveInside:evaluate -> already inside") end
    return false
  end

  -- If there exists an interior room, allow move inside.
  if entity_by_name ~= nil then
    local insideRoom = entity_by_name("Interior")
    if insideRoom and get_property ~= nil then
      if debug ~= nil then debug("MoveInside:evaluate -> interior exists") end
      return true
    end
  end

  if debug ~= nil then debug("MoveInside:evaluate -> no interior") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("MoveInside:simulate") end
  if agent_guid == nil or set_property == nil or entity_by_name == nil or get_property == nil then
    if debug ~= nil then debug("MoveInside:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local insideRoom = entity_by_name("Interior")
  if not insideRoom then return false end

  local ag = agent_guid()
  if ag and set_property ~= nil then
    set_property(ag, "CurrentRoom", insideRoom)
    local roomLoc = get_property(insideRoom, "Location")
    if roomLoc then set_property(ag, "Location", roomLoc) end
  end

  if debug ~= nil then debug("MoveInside:simulate -> moved agent inside") end
  return true
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
