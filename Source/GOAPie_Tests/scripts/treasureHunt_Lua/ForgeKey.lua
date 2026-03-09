-- ForgeKey.lua
-- At the Blacksmith, convert IronOre into TreasureKey.
-- Requires: IronOre in inventory, Blacksmith known, no TreasureKey yet.

local NullGuid = 0

local function tagLookup(tagName)
    local set = tag_set(tagName) or {}
    local lookup = {}
    for _, g in ipairs(set) do lookup[g] = true end
    return lookup
end

-- Check if agent has an item whose Info matches a given info entity name
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

    -- Blacksmith must be known
    local forgeGuid = entity_by_name("Blacksmith")
    if not forgeGuid then
        debug("ForgeKey.evaluate: Blacksmith not found -> FALSE")
        return false
    end
    local knownLookup = tagLookup("Known")
    if not knownLookup[forgeGuid] then
        debug("ForgeKey.evaluate: Blacksmith not known -> FALSE")
        return false
    end

    -- Must have IronOre
    if not hasItemWithInfo(agentGuid, "IronOreInfo") then
        debug("ForgeKey.evaluate: no IronOre -> FALSE")
        return false
    end

    -- Must not already have TreasureKey
    if hasItemWithInfo(agentGuid, "TreasureKeyInfo") then
        debug("ForgeKey.evaluate: already has key -> FALSE")
        return false
    end

    debug("ForgeKey.evaluate: can forge key -> TRUE")
    return true
end

function simulate(params)
    local agentGuid = params.agent.guid

    local forgeGuid = entity_by_name("Blacksmith")
    if not forgeGuid then return false end

    -- Move to Blacksmith
    local dist = move_agent_to_entity(forgeGuid)

    -- Create TreasureKey entity
    local keyInfoGuid = entity_by_name("TreasureKeyInfo")
    if not keyInfoGuid then return false end

    local forgeLoc = get_property(forgeGuid, "Location")
    local keyGuid = create_entity("TreasureKey")
    if not keyGuid then return false end

    tag_entity(keyGuid, "Item")
    tag_entity(keyGuid, "Known")
    set_property(keyGuid, "Location", forgeLoc or {0, 0, 0})
    set_property(keyGuid, "Info", keyInfoGuid)

    -- Add to inventory
    local inv = get_property(agentGuid, "Inventory") or {}
    table.insert(inv, keyGuid)
    set_property(agentGuid, "Inventory", inv)

    set_cost((dist or 0) + 5.0)
    debug("ForgeKey.simulate: forged key, dist=" .. tostring(dist))
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
