-- Inspect.lua
-- Inspect a Known, un-inspected Clue.
-- Opaque during planning (forceLeaf): marks clue as inspected and sets ExploredNewArea = true
-- but does NOT reveal targets during planning. Reveals happen during gameplay execution.

local NullGuid = 0

local function tagLookup(tagName)
    local set = tag_set(tagName) or {}
    local lookup = {}
    for _, g in ipairs(set) do lookup[g] = true end
    return lookup
end

function evaluate(params)
    local knownLookup = tagLookup("Known")
    local clueSet = tag_set("Clue") or {}

    for _, clueGuid in ipairs(clueSet) do
        if knownLookup[clueGuid] then
            local inspected = get_property(clueGuid, "Inspected")
            if inspected == false then
                debug("Inspect.evaluate: known un-inspected clue -> TRUE")
                return true
            end
        end
    end

    debug("Inspect.evaluate: no un-inspected clues -> FALSE")
    return false
end

function simulate(params)
    local agentGuid = params.agent.guid
    local agentLoc = get_property(agentGuid, "Location")
    if not agentLoc then return false end

    local knownLookup = tagLookup("Known")
    local clueSet = tag_set("Clue") or {}

    local bestGuid = nil
    local bestDist = math.huge

    for _, clueGuid in ipairs(clueSet) do
        if knownLookup[clueGuid] then
            local inspected = get_property(clueGuid, "Inspected")
            if inspected == false then
                local clueLoc = get_property(clueGuid, "Location")
                if clueLoc then
                    local dx = agentLoc[1] - clueLoc[1]
                    local dy = agentLoc[2] - clueLoc[2]
                    local dist = math.sqrt(dx*dx + dy*dy)
                    if dist < bestDist then
                        bestDist = dist
                        bestGuid = clueGuid
                    end
                end
            end
        end
    end

    if not bestGuid then return false end

    local dist = move_agent_to_entity(bestGuid)
    set_property(bestGuid, "Inspected", true)
    -- Opaque: do NOT reveal targets during planning
    set_property(agentGuid, "ExploredNewArea", true)

    set_cost((dist or 0) + 3.0)
    debug("Inspect.simulate: inspected clue, dist=" .. tostring(dist))
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
