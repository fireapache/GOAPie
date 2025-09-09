$base = "Source/GOAPie_Tests/scripts/example6_lua"
if (-not (Test-Path $base)) {
    New-Item -ItemType Directory -Path $base | Out-Null
}

$templates = @{
"DisablePower" = @'
-- DisablePower.lua
function evaluate(params)
    debug("DisablePower.evaluate called")
    return true
end

function simulate(params)
    debug("DisablePower.simulate: setting Power.Enabled = false if present")
    local p = entity_by_name("Power")
    if not p then return false end
    return set_property(p, "Enabled", false)
end

function heuristic(params)
    return 0
end
'@

"UseKey" = @'
-- UseKey.lua
function evaluate(params)
    debug("UseKey.evaluate called")
    return true
end

function simulate(params)
    debug("UseKey.simulate: attempt to consume Key item or set Safe unlocked")
    local key = entity_by_name("Key")
    if not key then return false end
    -- Example: set a property indicating key used
    return set_property(key, "Used", true)
end

function heuristic(params)
    return 0
end
'@

"Lockpick" = @'
-- Lockpick.lua
function evaluate(params)
    debug("Lockpick.evaluate called")
    return true
end

function simulate(params)
    debug("Lockpick.simulate: attempt lockpicking; success probabilistic placeholder")
    -- Placeholder: always succeed for now
    return true
end

function heuristic(params)
    return 1
end
'@

"BreakConnector" = @'
-- BreakConnector.lua
function evaluate(params)
    debug("BreakConnector.evaluate called")
    return true
end

function simulate(params)
    debug("BreakConnector.simulate: break connector -> set Broken=true")
    local conn = entity_by_name("Connector")
    if not conn then return false end
    return set_property(conn, "Broken", true)
end

function heuristic(params)
    return 1
end
'@

"CutBars" = @'
-- CutBars.lua
function evaluate(params)
    debug("CutBars.evaluate called")
    return true
end

function simulate(params)
    debug("CutBars.simulate: cut bars to allow entry")
    local bars = entity_by_name("Bars")
    if not bars then return false end
    return set_property(bars, "Cut", true)
end

function heuristic(params)
    return 1
end
'@

"EnterThrough" = @'
-- EnterThrough.lua
function evaluate(params)
    debug("EnterThrough.evaluate called")
    return true
end

function simulate(params)
    debug("EnterThrough.simulate: move agent inside through entry point")
    local entry = entity_by_name("EntryPoint")
    if not entry then return false end
    -- move_agent_to_entity returns distance if success
    local d = move_agent_to_entity(entry)
    return d ~= nil
end

function heuristic(params)
    return 0
end
'@

"SearchForItem" = @'
-- SearchForItem.lua
function evaluate(params)
    debug("SearchForItem.evaluate called")
    return true
end

function simulate(params)
    debug("SearchForItem.simulate: set property FoundItem=true on Agent if item exists")
    local item = entity_by_name("Key") -- example target
    if not item then return false end
    local agentGuid = entity_by_name("Agent")
    if not agentGuid then return false end
    return set_property(agentGuid, "HasKey", true)
end

function heuristic(params)
    return 1
end
'@

"MoveInside" = @'
-- MoveInside.lua
function evaluate(params)
    debug("MoveInside.evaluate called")
    return true
end

function simulate(params)
    debug("MoveInside.simulate: move agent to Safe location")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    local d = move_agent_to_entity(safe)
    return d ~= nil
end

function heuristic(params)
    -- small heuristic based on distance (real heuristic function exists too)
    return 0
end
'@

"OpenSafeWithKey" = @'
-- OpenSafeWithKey.lua
function evaluate(params)
    debug("OpenSafeWithKey.evaluate called")
    local agentHasKey = get_property(entity_by_name("Agent"), "HasKey")
    if agentHasKey == nil then return true end
    return agentHasKey == true
end

function simulate(params)
    debug("OpenSafeWithKey.simulate: open safe using key if present")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 0
end
'@

"OpenSafeWithCode" = @'
-- OpenSafeWithCode.lua
function evaluate(params)
    debug("OpenSafeWithCode.evaluate called")
    return true
end

function simulate(params)
    debug("OpenSafeWithCode.simulate: enter code to open safe (placeholder always succeeds)")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 2
end
'@

"CrackSafe" = @'
-- CrackSafe.lua
function evaluate(params)
    debug("CrackSafe.evaluate called")
    return true
end

function simulate(params)
    debug("CrackSafe.simulate: attempt to crack safe (placeholder)")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 5
end
'@

"BruteForceSafe" = @'
-- BruteForceSafe.lua
function evaluate(params)
    debug("BruteForceSafe.evaluate called")
    return true
end

function simulate(params)
    debug("BruteForceSafe.simulate: brute-force opening the safe (placeholder)")
    local safe = entity_by_name("Safe")
    if not safe then return false end
    return set_property(safe, "Opened", true)
end

function heuristic(params)
    return 10
end
'@
}

foreach ($kv in $templates.GetEnumerator()) {
    $name = $kv.Key
    $content = $kv.Value
    $path = Join-Path $base ("$name.lua")
    Write-Host "Writing $path"
    $content | Set-Content -Path $path -Encoding UTF8
}
