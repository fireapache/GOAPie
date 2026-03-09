-- MoveTo.lua
-- Move to a Known waypoint, preferring waypoints with unrevealed neighbors.

local NullGuid = 0

local function tagLookup(tagName)
    local set = tag_set(tagName) or {}
    local lookup = {}
    for _, g in ipairs(set) do lookup[g] = true end
    return lookup
end

function evaluate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local wpSet = tag_set("Waypoint") or {}
    local knownLookup = tagLookup("Known")

    for _, wpGuid in ipairs(wpSet) do
        if knownLookup[wpGuid] then
            local wpLoc = get_property(wpGuid, "Location")
            if wpLoc then
                local dx = agentLoc[1] - wpLoc[1]
                local dy = agentLoc[2] - wpLoc[2]
                local dist = math.sqrt(dx*dx + dy*dy)
                if dist > 1.0 then
                    debug("MoveTo.evaluate: reachable known waypoint -> TRUE")
                    return true
                end
            end
        end
    end

    debug("MoveTo.evaluate: already at all known waypoints -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local wpSet = tag_set("Waypoint") or {}
    local knownLookup = tagLookup("Known")

    local bestGuid = nil
    local bestScore = math.huge

    for _, wpGuid in ipairs(wpSet) do
        if knownLookup[wpGuid] then
            local wpLoc = get_property(wpGuid, "Location")
            if wpLoc then
                local dx = agentLoc[1] - wpLoc[1]
                local dy = agentLoc[2] - wpLoc[2]
                local dist = math.sqrt(dx*dx + dy*dy)
                if dist > 1.0 then
                    local score = dist
                    -- Prefer waypoints with unrevealed neighbors
                    local reveals = get_property(wpGuid, "Reveals") or {}
                    for _, rg in ipairs(reveals) do
                        if not knownLookup[rg] then
                            score = score - 50.0
                            break
                        end
                    end
                    if score < bestScore then
                        bestScore = score
                        bestGuid = wpGuid
                    end
                end
            end
        end
    end

    if not bestGuid then return false end

    local dist = move_agent_to_entity(bestGuid)
    set_cost((dist or 0) + 1.0)
    debug("MoveTo.simulate: moved, dist=" .. tostring(dist))
    return true
end

function heuristic(params)
    local agentGuid = params.agent.guid

    local chestGuid = entity_by_name("LockedChest")
    if not chestGuid then return 30.0 end

    local openVal = get_property(chestGuid, "Open")
    if openVal == true then return 0 end

    local h = 0
    local knownLookup = tagLookup("Known")
    if not knownLookup[chestGuid] then h = h + 30.0 end

    local inv = get_property(agentGuid, "Inventory") or {}
    local hasTreasureKey = false
    local hasIronOre = false
    local treasureKeyInfo = entity_by_name("TreasureKeyInfo")
    local ironOreInfo = entity_by_name("IronOreInfo")

    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == treasureKeyInfo then hasTreasureKey = true end
        if info and info == ironOreInfo then hasIronOre = true end
    end

    if not hasTreasureKey then h = h + 20.0 end
    if not hasIronOre then h = h + 10.0 end

    local agentLoc = get_property(agentGuid, "Location")
    local chestLoc = get_property(chestGuid, "Location")
    if agentLoc and chestLoc then
        local dx = agentLoc[1] - chestLoc[1]
        local dy = agentLoc[2] - chestLoc[2]
        h = h + math.sqrt(dx*dx + dy*dy) * 0.1
    end

    return h
end
