-- OpenSafeWithCode.lua
-- Open the safe by entering collected code pieces.
-- Precondition: in safe room, LockMode=1 (code-lock), alarm disabled,
--               agent has collected enough code pieces (RequiredCodePieces).

local NullGuid = 0

function evaluate(params)
    local agentGuid = params.agent.guid

    -- Must be in the safe room
    local currentRoom = get_property(agentGuid, "CurrentRoom")
    if not currentRoom or currentRoom == NullGuid then
        debug("OpenSafeWithCode.evaluate: not inside -> FALSE")
        return false
    end

    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    if get_property(safeGuid, "Open") == true then return false end

    local safeRoom = get_property(safeGuid, "InRoom")
    if currentRoom ~= safeRoom then
        debug("OpenSafeWithCode.evaluate: not in safe room -> FALSE")
        return false
    end

    if get_property(safeGuid, "LockMode") ~= 1.0 then
        debug("OpenSafeWithCode.evaluate: not code-lock -> FALSE")
        return false
    end

    -- Alarm must be disabled (code lock is wired to security system)
    local alarmGuid = entity_by_name("AlarmSystem")
    if alarmGuid and get_property(alarmGuid, "Armed") == true then
        debug("OpenSafeWithCode.evaluate: alarm still armed -> FALSE")
        return false
    end

    -- Count code pieces in inventory
    local required = get_property(safeGuid, "RequiredCodePieces") or 2
    local inv = get_property(agentGuid, "Inventory") or {}
    local codePieces = 0
    local codeInfos = {
        entity_by_name("CodePieceAInfo"),
        entity_by_name("CodePieceBInfo"),
        entity_by_name("CodePieceCInfo")
    }
    for _, itemGuid in ipairs(inv) do
        local info = get_property(itemGuid, "Info")
        if info then
            for _, ci in ipairs(codeInfos) do
                if info == ci then codePieces = codePieces + 1; break end
            end
        end
    end

    debug("OpenSafeWithCode.evaluate: pieces=" .. codePieces .. "/" .. tostring(required))
    if codePieces < required then
        debug("OpenSafeWithCode.evaluate: not enough pieces -> FALSE")
        return false
    end

    return true
end

function simulate(params)
    local safeGuid = entity_by_name("Safe")
    if not safeGuid then return false end
    set_property(safeGuid, "Open", true)
    set_cost(3.0)
    debug("OpenSafeWithCode.simulate: safe opened with code")
    return true
end

function heuristic(params)
    local safeGuid = entity_by_name("Safe")
    if safeGuid and get_property(safeGuid, "Open") == true then return 0 end
    return 0
end
