-- EnterThrough.lua
-- Starter Lua action for entering through a connector.
-- Uses C helper bindings when available (entity_by_name, tag_set, get_property, set_property, move_agent_to_entity, estimate_heuristic, debug).

function evaluate(params)
  if debug ~= nil then debug("EnterThrough:evaluate") end
  -- If helpers missing, be permissive.
  if entity_by_name == nil or tag_set == nil or get_property == nil then
    return true
  end

  local agent = nil
  if agent_guid ~= nil then agent = agent_guid() end

  local cur = nil
  if agent and get_property ~= nil then cur = get_property(agent, "CurrentRoom") end
  if cur and cur ~= 0 then
    if debug ~= nil then debug("EnterThrough:evaluate -> already inside") end
    return false
  end

  local connectors = tag_set("Connector")
  if not connectors then
    if debug ~= nil then debug("EnterThrough:evaluate -> no connectors") end
    return false
  end

  local alarmEntity = nil
  if entity_by_name ~= nil then alarmEntity = entity_by_name("AlarmSystem") end
  local alarmArmed = false
  if alarmEntity and get_property ~= nil then alarmArmed = get_property(alarmEntity, "Armed") end

  for i=1,#connectors do
    local g = connectors[i]
    local locked = get_property(g,"Locked")
    local blocked = get_property(g,"Blocked")
    local barred = get_property(g,"Barred")
    local alarmed = get_property(g,"Alarmed")
    local blockedAny = (locked or blocked or barred)
    if not blockedAny and not (alarmArmed and alarmed) then
      if debug ~= nil then debug("EnterThrough:evaluate -> viable connector found") end
      return true
    end
  end

  if debug ~= nil then debug("EnterThrough:evaluate -> none viable") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("EnterThrough:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil or move_agent_to_entity == nil then
    if debug ~= nil then debug("EnterThrough:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then return false end

  local best = nil
  local bestLen = 1e30
  for i=1,#connectors do
    local g = connectors[i]
    local locked = get_property(g,"Locked")
    local blocked = get_property(g,"Blocked")
    local barred = get_property(g,"Barred")
    local alarmed = get_property(g,"Alarmed")
    local blockedAny = (locked or blocked or barred)
    if not blockedAny then
      local len = move_agent_to_entity(g)
      if len and len < bestLen then bestLen = len; best = g end
    end
  end

  if not best then
    if debug ~= nil then debug("EnterThrough:simulate -> no target") end
    return false
  end

  -- Set CurrentRoom to connector.To and move agent location if helpers allow.
  local toGuid = get_property(best, "To")
  local ag = nil
  if agent_guid ~= nil then ag = agent_guid() end
  if ag and set_property ~= nil then
    set_property(ag, "CurrentRoom", toGuid)
    local roomLoc = get_property(toGuid, "Location")
    if roomLoc then set_property(ag, "Location", roomLoc) end
  end

  if debug ~= nil then debug("EnterThrough:simulate -> succeeded") end
  return true
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
