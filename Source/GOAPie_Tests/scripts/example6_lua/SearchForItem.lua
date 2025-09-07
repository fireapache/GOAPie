-- SearchForItem.lua
-- Placeholder Lua action: search containers/areas for a required item (e.g., safe code or key).

function evaluate(params)
  if debug ~= nil then debug("SearchForItem:evaluate") end
  if tag_set == nil or get_property == nil then
    return true
  end

  local containers = tag_set("Container")
  if not containers then
    if debug ~= nil then debug("SearchForItem:evaluate -> no containers") end
    return false
  end

  for i=1,#containers do
    local g = containers[i]
    local searched = get_property(g, "Searched")
    local contains = get_property(g, "ContainsItem")
    if not searched and contains then
      if debug ~= nil then debug("SearchForItem:evaluate -> item possibly available") end
      return true
    end
  end

  if debug ~= nil then debug("SearchForItem:evaluate -> nothing to search") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("SearchForItem:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("SearchForItem:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local containers = tag_set("Container")
  if not containers then return false end

  for i=1,#containers do
    local g = containers[i]
    local searched = get_property(g, "Searched")
    local contains = get_property(g, "ContainsItem")
    if not searched then
      set_property(g, "Searched", true)
      if contains then
        -- mark agent as having found the item
        local ag = nil
        if agent_guid ~= nil then ag = agent_guid() end
        if ag and set_property ~= nil then set_property(ag, "HasItem", true) end
        if debug ~= nil then debug("SearchForItem:simulate -> found item and assigned") end
        return true
      end
    end
  end

  if debug ~= nil then debug("SearchForItem:simulate -> not found") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
