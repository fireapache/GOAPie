# GOAPie

Header-only C++ library for Goal Oriented Action Planning (GOAP). Agents find action sequences to achieve goals by evaluating preconditions and effects using A* search.

## Features

- Header-only core library (C++20, namespace `gie`)
- A* planner with heuristic support and configurable depth limits
- Entity/property blackboard system with parent-chain isolation for simulation
- Lua 5.4 integration for hot-reloadable actions
- Visual testing environment (ImGui/OpenGL) with world view, planner debugger, entity outliner, and waypoint editor
- JSON serialization for world state persistence

## Building

Requires [Premake5](https://premake.github.io/) (v5.0.0-beta8).

```bash
# Generate project files
./premake5.exe vs2022    # or vs2026

# Build
msbuild GOAPie.sln /p:Configuration=Debug /p:Platform=x64

# Run tests (no arguments runs automated + visualization + example validation)
./Binaries/Tests-Debug-windows-x86_64/Tests.exe

# Run with visualization GUI
./Binaries/Tests-Debug-windows-x86_64/Tests.exe -v

# Run a specific example
./Binaries/Tests-Debug-windows-x86_64/Tests.exe -e 6 -v
```

## Project Structure

```
Source/GOAPie/include/    Core library headers
Source/GOAPie_Tests/      Test app (examples, visualization, automated tests)
Source/GOAPie_Tests/scripts/  Lua action scripts
```

### Dependencies (git submodules)

GLFW, GLAD, ImGui, ImGuiColorTextEdit, Lua 5.4, GLM, UUID_V4

## Examples

1. **fundamentals** - Basic action planning
2. **openDoor** - Simple precondition/effect chain
3. **cutDownTrees** - Resource gathering with purchases
4. **treesOnHill** - Pathfinding with waypoint graph
5. **survivalOnHill** - Multi-goal survival scenario
6. **heistOpenSafe** - Complex heist with rooms, connectors, alarms, and safe cracking (also available as Lua variant with `-lua`)

## License

MIT
