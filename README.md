# GOAPie

Header-only C++20 library for Goal Oriented Action Planning (GOAP). Agents find optimal action sequences to achieve goals via A* search over preconditions and effects.

## Core Concepts

- **World** -- owns entities, properties, and archetypes (blackboard pattern)
- **Entity / Agent** -- data containers identified by GUID, with typed properties and tags
- **ActionSimulator** -- defines `evaluate()` (preconditions) and `simulate()` (effects) for the planner, with optional `calculateHeuristic()`
- **Planner** -- runs A* search expanding simulations through available actions, backtracks to build the plan
- **Goal** -- target property values the planner tries to satisfy

Actions can be defined as **lambdas** (inline) or **class overrides** (subclassing `ActionSimulator`). Both styles work through the same base class.

### Lambda example

```cpp
planner.addLambdaAction( "OpenDoor",
    // evaluate: check preconditions
    []( gie::EvaluateSimulationParams params ) -> bool {
        auto door = params.simulation.world()->entity( /* guid */ );
        return door && !*door->property("Opened")->getBool();
    },
    // simulate: apply effects
    []( gie::SimulateSimulationParams params ) -> bool {
        auto ppt = params.simulation.context().property( /* guid */ );
        ppt->value = true;
        return true; // action auto-pushed when simulate succeeds
    }
);
```

### Features

- Header-only core (`Source/GOAPie/include/`, namespace `gie`)
- A* planner with heuristic support and configurable depth limits
- Blackboard with parent-chain isolation for simulation branching
- Lua 5.4 integration for hot-reloadable actions (`GIE_WITH_LUA`)
- JSON serialization for world state
- Visual test environment (ImGui/OpenGL) with planner debugger and entity outliner

## Building

Requires [Premake5](https://premake.github.io/) (v5.0.0-beta8). Windows / Visual Studio only.

```bash
./premake5.exe vs2026       # generates .slnx (use vs2022 for .sln)

msbuild GOAPie.slnx /p:Configuration=Debug /p:Platform=x64

./Binaries/Tests-Debug-windows-x86_64/Tests.exe       # automated tests
./Binaries/Tests-Debug-windows-x86_64/Tests.exe -v     # visualization GUI
./Binaries/Tests-Debug-windows-x86_64/Tests.exe -e 6 -v  # specific example
```

## Project Structure

```
Source/GOAPie/include/          Core library headers
Source/GOAPie_Tests/src/        Test app, examples, visualization
Source/GOAPie_Tests/scripts/    Lua action scripts
```

Dependencies (git submodules): GLFW, GLAD, ImGui, ImGuiColorTextEdit, Lua 5.4, GLM, UUID_V4

## Examples

1. **fundamentals** -- world, entity, and property basics (no planner)
2. **openDoor** -- single-action plan with distance cost
3. **cutDownTrees** -- resource gathering, purchasing, money management
4. **treesOnHill** -- waypoint navigation + resource gathering
5. **survivalOnHill** -- multi-goal survival (energy, hunger, thirst)
6. **heistOpenSafe** -- rooms, alarms, tools, safe cracking (also available as Lua variant with `-lua`)
7. **treasureHunt** -- exploration, clue following, discovery

## License

MIT
