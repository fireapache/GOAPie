-- OpenChest.lua
-- Open the LockedChest if agent has TreasureKey.
-- Requires: LockedChest known, not yet open, TreasureKey in inventory.

local NullGuid = 0

local function tagLookup(tagName)
    local set = tag_set(tagName) or {}
    local lookup = {}
    for _, g in ipairs(set) do lookup[g] = true end
    return lookup
end

local function hasItemWithInfo(agentGuid, infoName)
    local inv = get_property(agentGuid, "Inventory") or {}
    local infoGuid = entity_by_name(infoName)
    if not infoGuid then return false end
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info and info == infoGuid then return true end
    end
    return false
end

function evaluate(params)
    local agentGuid = params.agent.guid

    -- Chest must exist and be known
    local chestGuid = entity_by_name("LockedChest")
    if not chestGuid then
        debug("OpenChest.evaluate: chest not found -> FALSE")
        return false
    end
    local knownLookup = tagLookup("Known")
    if not knownLookup[chestGuid] then
        debug("OpenChest.evaluate: chest not known -> FALSE")
        return false
    end

    -- Must not already be open
    local openVal = get_property(chestGuid, "Open")
    if openVal == true then
        debug("OpenChest.evaluate: already open -> FALSE")
        return false
    end

    -- Must have TreasureKey
    if not hasItemWithInfo(agentGuid, "TreasureKeyInfo") then
        debug("OpenChest.evaluate: no key -> FALSE")
        return false
    end

    debug("OpenChest.evaluate: can open chest -> TRUE")
    return true
end

function simulate(params)
    local agentGuid = params.agent.guid

    local chestGuid = entity_by_name("LockedChest")
    if not chestGuid then return false end

    -- Move to chest
    local dist = move_agent_to_entity(chestGuid)

    -- Open the chest
    set_property(chestGuid, "Open", true)

    set_cost((dist or 0) + 2.0)
    debug("OpenChest.simulate: opened chest, dist=" .. tostring(dist))
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
