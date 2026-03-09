-- Observe.lua
-- Stand at a Known waypoint and reveal what it shows.
-- Opaque during planning (forceLeaf): sets ExploredNewArea = true but does not reveal entities.
-- Reveals happen only during gameplay execution in C++.

local NullGuid = 0

-- Build a lookup table from a tag_set result
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
                if dist < 1.0 then
                    -- Check if this waypoint has unrevealed entities
                    local reveals = get_property(wpGuid, "Reveals") or {}
                    for _, rg in ipairs(reveals) do
                        if not knownLookup[rg] then
                            debug("Observe.evaluate: unrevealed entities at waypoint -> TRUE")
                            return true
                        end
                    end
                end
            end
        end
    end

    debug("Observe.evaluate: nothing new to observe -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    set_property(agentGuid, "ExploredNewArea", true)
    set_cost(2.0)
    debug("Observe.simulate: opaque — no reveals during planning")
    return true
end

function heuristic(params)
    local agentGuid = params.agent.guid

    local chestGuid = entity_by_name("LockedChest")
    if not chestGuid then return 30.0 end

    local openVal = get_property(chestGuid, "Open")
    if openVal == true then return 0 end

    local h = 0

    -- +30 if chest not discovered (not known)
    local knownLookup = tagLookup("Known")
    if not knownLookup[chestGuid] then h = h + 30.0 end

    -- +20 if no TreasureKey in inventory
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

    -- + distance to chest
    local agentLoc = get_property(agentGuid, "Location")
    local chestLoc = get_property(chestGuid, "Location")
    if agentLoc and chestLoc then
        local dx = agentLoc[1] - chestLoc[1]
        local dy = agentLoc[2] - chestLoc[2]
        h = h + math.sqrt(dx*dx + dy*dy) * 0.1
    end

    return h
end
