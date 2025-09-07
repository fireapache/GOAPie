-- BreakConnector.lua
-- Placeholder Lua action: break a connector (forceful entry).

function evaluate(params)
  if debug ~= nil then debug("BreakConnector:evaluate") end
  if tag_set == nil or get_property == nil then
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then
    if debug ~= nil then debug("BreakConnector:evaluate -> no connectors") end
    return false
  end

  for i=1,#connectors do
    local g = connectors[i]
    local blocked = get_property(g, "Blocked")
    local breakable = get_property(g, "Breakable")
    if blocked and breakable then
      if debug ~= nil then debug("BreakConnector:evaluate -> breakable blocked connector found") end
      return true
    end
  end

  if debug ~= nil then debug("BreakConnector:evaluate -> nothing breakable") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("BreakConnector:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("BreakConnector:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then return false end

  for i=1,#connectors do
    local g = connectors[i]
    local blocked = get_property(g, "Blocked")
    local breakable = get_property(g, "Breakable")
    if blocked and breakable then
      set_property(g, "Blocked", false)
      if debug ~= nil then debug("BreakConnector:simulate -> broke connector") end
      return true
    end
  end

  if debug ~= nil then debug("BreakConnector:simulate -> failed") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
