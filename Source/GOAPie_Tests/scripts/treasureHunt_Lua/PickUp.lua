-- PickUp.lua
-- Collect a Known Item not already in the agent's inventory.

local NullGuid = 0

local function tagLookup(tagName)
    local set = tag_set(tagName) or {}
    local lookup = {}
    for _, g in ipairs(set) do lookup[g] = true end
    return lookup
end

function evaluate(params)
    local agentGuid = params.agent.guid
    local inv = get_property(agentGuid, "Inventory") or {}
    local knownLookup = tagLookup("Known")
    local itemSet = tag_set("Item") or {}

    local inInv = {}
    for _, g in ipairs(inv) do inInv[g] = true end

    for _, itemGuid in ipairs(itemSet) do
        if knownLookup[itemGuid] and not inInv[itemGuid] then
            debug("PickUp.evaluate: known item not in inventory -> TRUE")
            return true
        end
    end

    debug("PickUp.evaluate: no pickable items -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local inv = get_property(agentGuid, "Inventory") or {}
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local knownLookup = tagLookup("Known")
    local itemSet = tag_set("Item") or {}

    local inInv = {}
    for _, g in ipairs(inv) do inInv[g] = true end

    local bestGuid = nil
    local bestDist = math.huge

    for _, itemGuid in ipairs(itemSet) do
        if knownLookup[itemGuid] and not inInv[itemGuid] then
            local itemLoc = get_property(itemGuid, "Location")
            if itemLoc then
                local dx = agentLoc[1] - itemLoc[1]
                local dy = agentLoc[2] - itemLoc[2]
                local dist = math.sqrt(dx*dx + dy*dy)
                if dist < bestDist then
                    bestDist = dist
                    bestGuid = itemGuid
                end
            end
        end
    end

    if not bestGuid then return false end

    local dist = move_agent_to_entity(bestGuid)

    -- Add item to inventory
    table.insert(inv, bestGuid)
    set_property(agentGuid, "Inventory", inv)

    set_cost((dist or 0) + 1.0)
    debug("PickUp.simulate: picked up item, dist=" .. tostring(dist))
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
