-- UseKey.lua
-- Placeholder Lua action: use a key item to open/operate something.

function evaluate(params)
  if debug ~= nil then debug("UseKey:evaluate") end
  if entity_by_name == nil or tag_set == nil or get_property == nil then
    return true
  end

  local agent = nil
  if agent_guid ~= nil then agent = agent_guid() end

  -- If agent already has key (simulation), no need to use key
  if agent and get_property ~= nil then
    local hasKey = get_property(agent, "HasKey")
    if hasKey then
      if debug ~= nil then debug("UseKey:evaluate -> agent already has key") end
      return false
    end
  end

  local keys = tag_set("Key")
  if not keys then
    if debug ~= nil then debug("UseKey:evaluate -> no keys present") end
    return false
  end

  -- If any key is reachable/available, allow use
  for i=1,#keys do
    local k = keys[i]
    local taken = get_property(k, "Taken")
    if not taken then
      if debug ~= nil then debug("UseKey:evaluate -> key available") end
      return true
    end
  end

  if debug ~= nil then debug("UseKey:evaluate -> none available") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("UseKey:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil or move_agent_to_entity == nil then
    if debug ~= nil then debug("UseKey:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local keys = tag_set("Key")
  if not keys then return false end

  -- Find first untaken key and "take" it in simulation, assign to agent.
  for i=1,#keys do
    local k = keys[i]
    local taken = get_property(k, "Taken")
    if not taken then
      -- mark key taken and assign to agent
      set_property(k, "Taken", true)
      local ag = nil
      if agent_guid ~= nil then ag = agent_guid() end
      if ag and set_property ~= nil then
        set_property(ag, "HasKey", true)
      end
      if debug ~= nil then debug("UseKey:simulate -> key taken and assigned") end
      return true
    end
  end

  if debug ~= nil then debug("UseKey:simulate -> no key taken") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
