-- CutBars.lua
-- Placeholder Lua action: cut bars to unblock a connector or room.

function evaluate(params)
  if debug ~= nil then debug("CutBars:evaluate") end
  if tag_set == nil or get_property == nil then
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then
    if debug ~= nil then debug("CutBars:evaluate -> no connectors") end
    return false
  end

  for i=1,#connectors do
    local g = connectors[i]
    local barred = get_property(g, "Barred")
    local cuttable = get_property(g, "Cuttable")
    if barred and cuttable then
      if debug ~= nil then debug("CutBars:evaluate -> barred & cuttable connector found") end
      return true
    end
  end

  if debug ~= nil then debug("CutBars:evaluate -> nothing to cut") end
  return false
end

function simulate(params)
  if debug ~= nil then debug("CutBars:simulate") end
  if tag_set == nil or get_property == nil or set_property == nil then
    if debug ~= nil then debug("CutBars:simulate -> helpers missing, succeed permissively") end
    return true
  end

  local connectors = tag_set("Connector")
  if not connectors then return false end

  for i=1,#connectors do
    local g = connectors[i]
    local barred = get_property(g, "Barred")
    local cuttable = get_property(g, "Cuttable")
    if barred and cuttable then
      set_property(g, "Barred", false)
      if debug ~= nil then debug("CutBars:simulate -> cut bars") end
      return true
    end
  end

  if debug ~= nil then debug("CutBars:simulate -> failed") end
  return false
end

function heuristic(params)
  if estimate_heuristic ~= nil then return estimate_heuristic() end
  return 0
end
