# Lunaris — Design Document

## 1. Overview

`Lunaris` is a standalone C++23 library that provides a modern, type-safe and ergonomic C++ wrapper around Luau.

The wrapper is designed to be reusable outside Axiom while providing the abstractions Axiom needs for embedding Luau in a game engine.

```text
Application
    │
    ▼
Lunaris
    │
    ▼
Luau
```

Axiom-specific systems such as VFS, ECS, Reflection, Scheduler, Renderer, Physics and Engine APIs remain outside the generic wrapper and integrate with it through extension layers.

---

# 2. Goals

The wrapper should provide:

- RAII-based Luau state management
- C++ type-safe value access
- convenient stack access
- global access
- C++ function binding
- C++ userdata/usertype binding
- constructors
- properties
- methods
- overloads
- enums and flags
- error handling
- custom allocator support
- compiler and bytecode handling
- bytecode caching
- standalone-script management
- module management
- hot reloading for both normal scripts and modules
- optional generic libraries
- threading primitives needed by the Axiom scheduler
- Actor-style execution
- SharedTable and SharedTableRegistry
- Futures
- optional GLM extension
- a clean foundation for a future Reflection integration
- a comprehensive automated test suite based on GoogleTest (gtest)
- CI validation across supported compilers and platforms

The wrapper should minimize abstraction overhead and remain close enough to Luau that users can reason about what happens underneath.

---

# 3. Non-Goals

The generic wrapper must not depend on:

- Axiom ECS
- Axiom VFS
- Axiom Reflection
- Axiom Scheduler
- Axiom Renderer
- GLM
- Box2D
- Jolt
- OpenAL
- Axiom Asset Manager

Those systems can integrate with the wrapper from Axiom.

The wrapper also does not implement its own VM. Luau remains the VM, compiler and runtime.

---

# 4. Core Architecture

```text
Axiom (consumer)
 │
 ├── VFS
 ├── Script Manager
 ├── Reflection
 ├── Task Scheduler
 └── Engine APIs
 │
 ▼
Lunaris
 │
 ├── State
 ├── Value
 ├── Table
 ├── Function
 ├── Thread
 ├── Userdata
 ├── Error
 ├── Allocator
 │
 ├── Compiler
 ├── Bytecode
 ├── BytecodeCache
 │
 ├── Script
 ├── Module
 ├── ScriptManager
 └── ModuleCache
 │
 ▼
Luau
```

---

# 5. Namespace

The primary namespace is:

```cpp
lunaris
```

Optional extensions use nested namespaces:

```cpp
lunaris::extensions::glm
```

Threading-related wrapper APIs are located under:

```cpp
lunaris::libraries::threading
```

---

# 6. State

Example:

```cpp
lunaris::State lua;
```

The State owns a Luau `lua_State` and manages its lifetime through RAII.

A State may contain multiple scripts and modules:

```text
State
 ├── Script A
 ├── Script B
 ├── Script C
 └── Module Cache
```

A Script does not equal a VM State.

---

# 7. State Configuration

The State can be created with configuration:

```cpp
lunaris::StateConfig config;

config.allocator = ...;

lunaris::State lua(config);
```

Possible future configuration options include:

- allocator
- compiler configuration
- debugging configuration
- stack configuration
- library configuration

The configuration must remain extensible without breaking the public API.

---

# 8. Custom Allocator

The wrapper exposes Luau's custom allocator facility.

Conceptually:

```cpp
class Allocator
{
public:
    using AllocateFn = /* Luau allocator signature */;

    AllocateFn allocate = nullptr;
    void* userData = nullptr;
};
```

The exact function signature must match the Luau version used by the repository.

Example:

```cpp
StateConfig config;

config.allocator = {
    &AxiomAllocator,
    profilerContext
};

State lua(config);
```

The allocator can record:

- allocation
- reallocation
- free
- old size
- new size
- current memory
- peak memory
- allocation count

This enables:

```text
Luau
  ↓
Lunaris Allocator
  ↓
Axiom Memory System
  ↓
Profiler
```

The wrapper does not make a user-provided allocator thread-safe.

The `userData` object must remain alive for the lifetime of the State.

A running State should not arbitrarily replace its allocator. Preferred lifecycle:

```text
create allocator
      ↓
create State
      ↓
use State
      ↓
destroy State
      ↓
destroy allocator
```

---

# 9. Values and Stack

The wrapper provides type-safe access to the Luau stack.

Direct Luau API:

```cpp
lua_pushnumber(L, 42.0);
lua_pushstring(L, "Hello");

double number = lua_tonumber(L, -2);
const char* text = lua_tostring(L, -1);

lua_pop(L, 2);
```

Lunaris:

```cpp
lua.push(42.0);
lua.push("Hello");

double number = lua.get<double>(-2);
std::string text = lua.get<std::string>(-1);

lua.pop(2);
```

The wrapper should not hide the existence of the Luau stack completely. Low-level stack access remains useful for advanced integrations.

A higher-level `Value` abstraction is also provided.

---

# 10. Globals

Direct Luau API:

```cpp
lua_pushstring(L, "Axiom");
lua_setglobal(L, "EngineName");

lua_getglobal(L, "EngineName");

const char* name = lua_tostring(L, -1);

lua_pop(L, 1);
```

Lunaris:

```cpp
lua["EngineName"] = "Axiom";

std::string name =
    lua["EngineName"].get<std::string>();
```

---

# 11. Functions

C++ functions can be exposed directly.

Example:

```cpp
int Add(int a, int b)
{
    return a + b;
}
```

Lunaris:

```cpp
lua["Add"] = [](int a, int b)
{
    return a + b;
};
```

Luau:

```lua
local result = Add(10, 20)
```

The wrapper handles:

- argument conversion
- type checking
- return value conversion
- stack management
- error propagation

---

# 12. Calling Luau Functions

Given:

```lua
function Multiply(a, b)
    return a * b
end
```

Lunaris:

```cpp
auto result =
    lua["Multiply"]
        .call(6, 7)
        .get<int>();
```

The direct Luau equivalent requires explicit stack management:

```cpp
lua_getglobal(L, "Multiply");

lua_pushinteger(L, 6);
lua_pushinteger(L, 7);

int status = lua_pcall(L, 2, 1, 0);

if (status != 0)
{
    // error on stack
}

int result =
    lua_tointeger(L, -1);

lua_pop(L, 1);
```

---

# 13. C++ Usertypes

Example C++ type:

```cpp
struct Player
{
    std::string name;
    int health = 100;

    void Damage(int amount)
    {
        health -= amount;
    }
};
```

Lunaris:

```cpp
lua.new_usertype<Player>(
    "Player",

    constructor<Player>(),

    property("Name", &Player::name),
    property("Health", &Player::health),

    method("Damage", &Player::Damage)
);
```

Luau:

```lua
local player = Player.new()

player.Name = "Daniel"
player.Health = 100

player:Damage(25)

print(player.Name)
print(player.Health)
```

The wrapper manages userdata, metatables, type checks and dispatch.

---

# 14. Userdata Sharing

Userdata can declare its sharing semantics:

```cpp
enum class UserdataSharing
{
    Local,
    Transferable,
    Shared
};
```

Meaning:

### Local

Only the originating Luau State/Actor may access it.

### Transferable

The value can be transferred/copied between execution contexts according to its registered semantics.

### Shared

The same C++ object can be accessed concurrently. The underlying C++ type is responsible for being thread-safe.

The wrapper does not magically make arbitrary C++ objects thread-safe.

---

# 15. Constructors

Example:

```cpp
lua.new_usertype<Vector>(
    "Vector",

    constructors<
        Vector(),
        Vector(float, float, float)
    >()
);
```

Luau:

```lua
local a = Vector.new()
local b = Vector.new(1, 2, 3)
```

---

# 16. Properties

Properties can be read/write:

```cpp
property(
    "Health",
    &Player::GetHealth,
    &Player::SetHealth
);
```

Or read-only:

```cpp
property(
    "Name",
    &Player::GetName
);
```

---

# 17. Overloads

The wrapper supports overloaded C++ functions:

```cpp
lua["Create"] =
    overload(
        [](int value) { ... },
        [](float value) { ... },
        [](std::string_view value) { ... }
    );
```

The wrapper performs the appropriate argument dispatch.

---

# 18. Enums

Runtime enum support:

```lua
local PlayerState = enum.new("PlayerState", {
    Idle = 0,
    Running = 1,
    Jumping = 2,
    Dead = 3,
})
```

Flags:

```lua
local RenderFlags = enum.flags("RenderFlags", {
    Visible = 1 << 0,
    CastShadow = 1 << 1,
})
```

Runtime enum values should preferably be immutable.

Enums are a wrapper/library feature. No changes to the Luau parser or language are required.

---

# 19. Error Handling

Errors should be represented as structured C++ errors.

Conceptually:

```cpp
enum class ErrorType
{
    Compile,
    Load,
    Runtime,
    Type,
    Memory
};
```

```cpp
class Error
{
public:
    ErrorType type() const;

    std::string_view message() const;
    std::string_view source() const;

    int line() const;
    int column() const;
};
```

The preferred primary API is a `Result<T, Error>`-style interface.

Example:

```cpp
auto result =
    script.execute();

if (!result)
{
    const auto& error =
        result.error();

    AXIOM_ERROR(
        "{}:{}:{}: {}",
        error.source(),
        error.line(),
        error.column(),
        error.message()
    );
}
```

Function calls can use the same mechanism:

```cpp
auto result =
    lua["Update"].call(0.016f);

if (!result)
{
    AXIOM_ERROR(
        "Luau Update failed: {}",
        result.error().message()
    );
}
```

An optional exception-based convenience API may exist:

```cpp
lua["Update"].call_or_throw(0.016f);
```

but the non-throwing Result API is preferred for engine code.

---

# 20. Compiler and Bytecode

Luau source is compiled to bytecode and then loaded into the VM.

Lunaris exposes this as:

```cpp
auto bytecode =
    lua.compile(source);

auto chunk =
    lua.load(bytecode);
```

The exact bytecode representation follows the Luau version used by the repository.

`Bytecode` is an explicit wrapper type.

---

# 21. Bytecode Cache

The wrapper provides a generic bytecode cache.

```cpp
struct BytecodeCacheKey
{
    uint64_t sourceHash;
    uint64_t compilerVersion;
    uint64_t bytecodeVersion;
};
```

An entry can contain:

```cpp
struct BytecodeCacheEntry
{
    Bytecode bytecode;

    uint64_t sourceHash;
    uint64_t compilerVersion;
    uint64_t bytecodeVersion;

    std::string sourceIdentifier;
};
```

Source hashes are preferred over timestamps as the primary cache validation mechanism.

The cache must account for the compiler and bytecode version.

---

# 22. Bytecode Cache Storage

The cache should not require a physical filesystem.

```cpp
class BytecodeCacheStorage
{
public:
    virtual ~BytecodeCacheStorage() = default;

    virtual bool load(
        std::string_view key,
        Bytecode& output) = 0;

    virtual void store(
        std::string_view key,
        const Bytecode& bytecode) = 0;

    virtual void remove(
        std::string_view key) = 0;
};
```

Possible implementations:

```text
MemoryCache
FileCache
VFSCache
AssetPackCache
```

Axiom can implement its own storage using its VFS.

---

# 23. Scripts

A normal script is a standalone Luau chunk.

Example:

```lua
print("Player script loaded")

function Update(dt)
    print("Update:", dt)
end
```

It does not require:

- `return`
- `require`
- a module table
- special syntax

Each script can have its own environment.

```text
State
 ├── Script A → Environment A
 ├── Script B → Environment B
 └── Script C → Environment C
```

---

# 24. Script Hot Reload

All scripts are hot-reloadable, including scripts that are not modules.

Example:

```cpp
auto script =
    lua.load_script(
        "Player",
        source
    );

auto result =
    script.execute();
```

Later:

```cpp
auto result =
    script.reload(newSource);
```

Conceptually:

```text
Source changed
      ↓
Hash
      ↓
Bytecode Cache
      ↓
Compile if necessary
      ↓
Load new chunk
      ↓
Environment handling
      ↓
Execute
```

Hot Reload is therefore not limited to the module system.

---

# 25. Script Environments

Each standalone script has an environment.

```text
Script A
 └── Environment A

Script B
 └── Environment B

Script C
 └── Environment C
```

Script-local globals remain isolated unless explicitly shared.

The engine can provide common globals through the environment's fallback mechanism.

---

# 26. Reload Modes

Possible reload policies:

```cpp
enum class ReloadMode
{
    Reset,
    PreserveEnvironment,
    PreserveGlobals
};
```

### Reset

Destroy the old environment and create a new one.

### PreserveEnvironment

Reuse the environment while executing the new chunk.

### PreserveGlobals

Explicitly preserve selected globals.

The wrapper should not try to magically migrate arbitrary Lua state.

Existing closures can still reference old upvalues. Therefore:

```text
Source Reload
≠
automatic transformation of every existing closure
```

---

# 27. Modules

Modules use the same compiler and bytecode infrastructure.

Example:

```lua
local Player = require("Player")
```

The Module Manager maintains:

```text
Module Identifier
      ↓
Canonical Asset ID
      ↓
Module Cache
      ↓
Bytecode Cache
```

Axiom can map:

```text
game://scripts/Player.luau
```

to a canonical module identifier.

---

# 28. Module Hot Reload

Modules can be invalidated:

```cpp
lua.modules().invalidate("Player");
```

or reloaded:

```cpp
lua.modules().reload("Player");
```

The Module Manager can maintain a dependency graph:

```text
Game
├── Player
│   └── Character
│       └── Math
│
└── Inventory
    └── Item
```

Dependency tracking belongs to the Module Manager, not the Bytecode Cache.

---

# 29. Unified Script/Module Pipeline

Standalone script:

```text
Script
 ↓
Source
 ↓
Bytecode Cache
 ↓
Chunk
 ↓
Script Environment
 ↓
Execute
```

Module:

```text
Module
 ↓
Source
 ↓
Bytecode Cache
 ↓
Module Cache
 ↓
Module Environment
 ↓
Execute
```

Both use the same compiler and bytecode infrastructure.

---

# 30. Axiom VFS Integration

Axiom already provides a VFS. The generic wrapper must not depend directly on it.

Architecture:

```text
Axiom (consumer)
 │
 ├── VFS
 │
 ├── File Watcher
 │
 └── ScriptManager
 │
 ▼
Lunaris
 │
 ▼
Luau
```

Example Axiom-side loading:

```cpp
auto source =
    vfs.ReadFile("game://scripts/Player.luau");

auto script =
    lua.load_script(
        "Player",
        source
    );

auto result =
    script.execute();
```

A higher-level Axiom integration may provide:

```cpp
auto script =
    lua.load_script(
        vfs,
        "game://scripts/Player.luau"
    );
```

However, this overload is an Axiom integration and not part of the generic standalone wrapper.

The wrapper can alternatively expose a generic Source Provider interface, allowing Axiom's VFS to be connected without adding a dependency on Axiom.

---

# 31. File Watching and Reload

The File Watcher belongs to Axiom.

```text
Axiom VFS
    ↓
File Watcher
    ↓
ScriptManager
    ↓
Lunaris
    ↓
Script.reload()
```

Axiom can therefore detect:

```text
game://scripts/Player.luau changed
```

and call:

```cpp
script.reload(newSource);
```

The wrapper itself does not need to know how the source was obtained.

---

# 32. Direct Luau API vs. Lunaris

The following examples perform the same basic operations.

## State

Direct Luau:

```cpp
lua_State* L = luaL_newstate();

luaL_openlibs(L);
```

Lunaris:

```cpp
lunaris::State lua;

lua.open_libraries(
    Library::Base |
    Library::Table |
    Library::String
);
```

## Stack

Direct Luau:

```cpp
lua_pushnumber(L, 42);
lua_pushstring(L, "Hello");

double number = lua_tonumber(L, -2);
const char* text = lua_tostring(L, -1);

lua_pop(L, 2);
```

Lunaris:

```cpp
lua.push(42.0);
lua.push("Hello");

double number = lua.get<double>(-2);
std::string text = lua.get<std::string>(-1);

lua.pop(2);
```

## Global

Direct Luau:

```cpp
lua_pushinteger(L, 100);
lua_setglobal(L, "Answer");

lua_getglobal(L, "Answer");

int answer =
    lua_tointeger(L, -1);

lua_pop(L, 1);
```

Lunaris:

```cpp
lua["Answer"] = 100;

int answer =
    lua["Answer"].get<int>();
```

## C++ function

Direct Luau:

```cpp
int Lua_Add(lua_State* L)
{
    int a = luaL_checkinteger(L, 1);
    int b = luaL_checkinteger(L, 2);

    lua_pushinteger(L, a + b);

    return 1;
}

lua_pushcfunction(L, Lua_Add);
lua_setglobal(L, "Add");
```

Lunaris:

```cpp
lua["Add"] = [](int a, int b)
{
    return a + b;
};
```

## Calling a Luau function

Direct Luau:

```cpp
lua_getglobal(L, "Multiply");

lua_pushinteger(L, 6);
lua_pushinteger(L, 7);

int status =
    lua_pcall(L, 2, 1, 0);

if (status != 0)
{
    const char* error =
        lua_tostring(L, -1);

    // Handle error
    lua_pop(L, 1);
}
else
{
    int result =
        lua_tointeger(L, -1);

    lua_pop(L, 1);
}
```

Lunaris:

```cpp
auto result =
    lua["Multiply"]
        .call(6, 7)
        .get<int>();
```

Or using the Result API:

```cpp
auto result =
    lua["Multiply"].call(6, 7);

if (!result)
{
    AXIOM_ERROR(
        "Luau error: {}",
        result.error().message()
    );
}
```

## C++ Struct

Direct Luau requires userdata, metatables, C functions, type checks and stack handling.

Lunaris:

```cpp
lua.new_usertype<Player>(
    "Player",

    constructor<Player>(),

    property("Name", &Player::name),
    property("Health", &Player::health),

    method("Damage", &Player::Damage)
);
```

Luau:

```lua
local player = Player.new()

player.Name = "Daniel"
player.Health = 100

player:Damage(25)

print(player.Name)
print(player.Health)
```

## Script

With direct Luau, the host has to read the source, compile it, load the bytecode and execute the chunk:

```cpp
auto source =
    ReadFile("scripts/Player.luau");

lua_CompileOptions options{};

std::string bytecode =
    luau_compile(
        source.data(),
        source.size(),
        &options
    );

int status =
    luau_load(
        L,
        "Player",
        bytecode.data(),
        bytecode.size(),
        0
    );

if (status != 0)
{
    const char* error =
        lua_tostring(L, -1);

    // Handle load error
    lua_pop(L, 1);
}
else
{
    status =
        lua_pcall(L, 0, 0, 0);

    if (status != 0)
    {
        const char* error =
            lua_tostring(L, -1);

        // Handle runtime error
        lua_pop(L, 1);
    }
}
```

Axiom:

```cpp
auto source =
    vfs.ReadFile(
        "game://scripts/Player.luau"
    );

auto script =
    lua.load_script(
        "Player",
        source
    );

auto result =
    script.execute();

if (!result)
{
    AXIOM_ERROR(
        "{}:{}:{}: {}",
        result.error().source(),
        result.error().line(),
        result.error().column(),
        result.error().message()
    );
}
```

The important distinction is that `Lunaris` does not replace the Luau VM. It provides a higher-level C++ embedding API and manages repetitive binding and script-lifecycle work.

---

# 33. Comparison Summary

| Task | Direct Luau API | Lunaris |
|---|---|---|
| State | `lua_State*` | `State` |
| Lifetime | manual | RAII |
| Stack | `lua_push*`, `lua_to*`, `lua_pop` | `push()`, `get<T>()`, `pop()` |
| Globals | `lua_getglobal` / `lua_setglobal` | `lua["Name"]` |
| C++ functions | `lua_CFunction` + stack | C++ lambdas/functions |
| Userdata | manual | `new_usertype<T>()` |
| Properties | metatables/C functions | `property()` |
| Methods | metatables/C functions | `method()` |
| Constructors | manual | `constructor<T>()` |
| Overloads | manual dispatch | `overload()` |
| Function calls | stack + `lua_pcall` | `.call()` |
| Errors | status + error on stack | `Result<T, Error>` |
| Source loading | host implementation | `Script` API |
| Compiler | `luau_compile()` | `compile()` |
| Bytecode loading | `luau_load()` | `load()` |
| Bytecode cache | host implementation | `BytecodeCache` |
| Script lifecycle | host implementation | `Script` |
| Script hot reload | host implementation | `Script::reload()` |
| Module cache | host implementation | `ModuleCache` |
| Module hot reload | host implementation | Module API |
| VFS | external host system | Axiom integration |
| File watcher | external host system | Axiom integration |

The key principle is:

```text
Direct Luau API
    ↓
Low-level VM embedding
    ↓
Lunaris
    ↓
Type-safe C++ API
+ Binding API
+ Error API
+ Script lifecycle
+ Bytecode caching
+ Hot reload
+ Module management
```

---

# 34. Libraries

Generic libraries:

```text
bit32
buffer
math
string
table
time
threading
```

Potential later libraries:

```text
json
base64
uuid
timer
```

Engine-specific systems remain outside the generic library set:

```text
filesystem
networking
renderer
physics
audio
ecs
assets
input
```

`task` is an Axiom Engine API.

---

# 35. Threading

The wrapper does not create its own thread pool.

Axiom owns:

- worker threads
- fibers
- task queues
- work stealing
- scheduling
- actor scheduling

The wrapper provides the primitives required by the scheduler.

C++:

```cpp
auto future =
    scheduler.submit([] {
        return expensive_calculation();
    });

auto result =
    future.get();
```

Luau:

```lua
local future = task.spawn(function()
    return expensiveCalculation()
end)

local result = future:await()
```

---

# 36. Actor

The Actor API follows the conceptual Roblox model:

```lua
actor:BindToMessage(
    "Update",
    function(data)
        ...
    end
)

actor:BindToMessageParallel(
    "Generate",
    function(data)
        ...
    end
)

actor:SendMessage(
    "Generate",
    data
)
```

An Actor is not an OS thread.

It is an isolated Luau execution context scheduled by the engine scheduler.

---

# 37. Message Passing

Message passing is the primary cross-Actor communication mechanism.

```lua
actor:SendMessage(
    "SpawnEnemy",
    enemyData
)
```

Request/response can be built on top:

```lua
local future =
    actor:Request(
        "Calculate",
        data
    )

local result =
    future:await()
```

Arbitrary cross-State closure calls are not the primary communication mechanism.

---

# 38. SharedTable

SharedTable is distinct from an ordinary Lua table.

It should not simply be implemented as a normal table protected by a mutex.

Example:

```lua
local data =
    SharedTable.new()

SharedTableRegistry:SetSharedTable(
    "WorldData",
    data
)

local data2 =
    SharedTableRegistry:GetSharedTable(
        "WorldData"
    )
```

Registry lifetime is process/engine-wide.

The registry supports:

```lua
SharedTableRegistry:SetSharedTable("WorldData", data)
SharedTableRegistry:GetSharedTable("WorldData")
SharedTableRegistry:SetSharedTable("WorldData", nil)
```

---

# 39. SharedTable Value Rules

Supported values should be explicit.

Possible default values:

- nil
- boolean
- number
- string
- SharedTable
- explicitly transferable userdata

Avoid arbitrary:

- normal tables
- functions
- closures
- threads
- States
- arbitrary engine handles

Userdata sharing is controlled by `UserdataSharing`.

---

# 40. GLM Extension

GLM is not a dependency of the core wrapper.

```text
lunaris
```

remains GLM-independent.

Optional:

```text
lunaris::extensions::glm
```

Example:

```cpp
luau_glm::register_types(lua);
```

---

# 41. CFrame

`CFrame` is an Axiom Engine API, not a generic wrapper type.

Example API:

```lua
CFrame.new()
CFrame.new(x, y, z)
CFrame.new(position)

CFrame.fromAxisAngle(axis, angle)
CFrame.fromEulerAnglesXYZ(x, y, z)
```

Properties:

```lua
cf.Position
cf.Rotation
```

Operations:

```lua
cf:ToWorldSpace(other)
cf:ToObjectSpace(other)

cf:PointToWorldSpace(point)
cf:PointToObjectSpace(point)

cf:VectorToWorldSpace(vector)
cf:VectorToObjectSpace(vector)
```

Multiplication:

```lua
local result = a * b
local position = cf * vector
```

The implementation may internally use GLM but the public API should not expose GLM types.

---

# 42. Reflection

Reflection sits above the wrapper.

```text
Reflection Metadata
       │
       ├──→ LuauReflectionBinder
       │
       └──→ LuauDeclarationGenerator
                    ↓
                 .d.luau
```

The wrapper itself does not contain Axiom Reflection.

---

# 43. Performance

The wrapper should minimize overhead.

Goals:

- direct forwarding to Luau where possible
- minimal temporary allocations
- no unnecessary `std::function`
- templates for binding
- cached binding metadata
- bytecode caching
- optional allocator profiling
- no mutex-based VM safety
- predictable lifetime management

The wrapper should benchmark common operations against direct Luau C API usage.

The goal is not necessarily zero abstraction cost in every operation, but the abstraction should not introduce unnecessary overhead.

---

# 44. Testing Strategy (GoogleTest)

Lunaris uses **GoogleTest (gtest)** as its unit- and integration-test framework. Tests are part of the library architecture and must be introduced alongside each feature, not postponed until the implementation is complete.

## 44.1 Build Configuration

CMake options:

```cmake
option(LUNARIS_BUILD_TESTS "Build Lunaris tests" ON)
option(LUNARIS_BUILD_EXAMPLES "Build Lunaris examples" ON)
option(LUNARIS_BUILD_BENCHMARKS "Build Lunaris benchmarks" OFF)

if (LUNARIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

Use `GoogleTest::gtest_main` and `gtest_discover_tests()` so tests are discoverable by CTest and IDEs. Pin the GoogleTest version. To keep normal builds reproducible and avoid requiring network access, the preferred default is a pinned source copy under `thirdparty/googletest/`; an opt-in `FetchContent` path may be offered for contributors who prefer downloading it during configuration. GoogleTest is a development-only dependency and must never become a public or runtime dependency of Lunaris.

## 44.2 Test Layout

```text
tests/
├── CMakeLists.txt
├── Unit/
│   ├── StateTests.cpp
│   ├── ValueTests.cpp
│   ├── StackTests.cpp
│   ├── TableTests.cpp
│   ├── FunctionTests.cpp
│   ├── BindingTests.cpp
│   ├── UserdataTests.cpp
│   ├── AllocatorTests.cpp
│   ├── ErrorTests.cpp
│   ├── CompilerTests.cpp
│   ├── BytecodeTests.cpp
│   ├── BytecodeCacheTests.cpp
│   ├── ScriptTests.cpp
│   ├── ScriptReloadTests.cpp
│   ├── ModuleTests.cpp
│   ├── ModuleCacheTests.cpp
│   ├── LibraryTests.cpp
│   ├── SharedTableTests.cpp
│   ├── FutureTests.cpp
│   └── ActorTests.cpp
├── Integration/
│   ├── ScriptPipelineTests.cpp
│   ├── ModuleDependencyTests.cpp
│   ├── HotReloadTests.cpp
│   ├── BytecodeCachePersistenceTests.cpp
│   └── SchedulerIntegrationTests.cpp
├── TestUtils/
│   ├── LuauTestFixture.hpp
│   ├── TestAllocator.hpp
│   ├── InMemoryCacheStorage.hpp
│   └── TestSourceProvider.hpp
└── data/
    └── scripts/
```

## 44.3 Unit-Test Coverage

| Area | Required cases |
|---|---|
| State and lifetime | creation, destruction, move semantics if supported, configuration, repeated create/destroy |
| Values and stack | supported conversions, invalid conversions, stack indices, stack balance after success and failure |
| Tables and functions | get/set, missing keys, argument/return conversion, protected calls, error propagation |
| Bindings | free functions, lambdas, member methods, const methods, properties, read-only properties, constructors, overload resolution |
| Userdata | type checks, ownership, destruction, Local/Transferable/Shared policy enforcement |
| Allocator | allocate/reallocate/free accounting, peak/current bytes, failed allocation behavior, user-data lifetime |
| Errors | compile, load, runtime, type and memory errors; source/line/column when Luau provides them |
| Compiler/bytecode | valid and invalid source, compile/load round trip, version compatibility checks |
| Bytecode cache | hit, miss, source-hash invalidation, compiler-version invalidation, bytecode-version invalidation, storage failures |
| Scripts | isolated environments, execute, reset, repeated execution, reload modes |
| Modules | one-time load, cache hit, failure caching policy, dependency invalidation and reload |
| Libraries | individual registration, combined flags, repeated registration behavior, unavailable library behavior |
| SharedTable | allowed/rejected value types, nested SharedTables, registry set/get/remove, concurrent access contract |
| Futures/Actors | ready/pending states, result and error propagation, cancellation semantics, message delivery, ordering guarantees |

Every bug fix should add a regression test that fails before the fix and passes afterward. Tests must not rely on timing sleeps where synchronization primitives or deterministic test schedulers can be used.

## 44.4 Integration Tests

Integration tests validate complete feature paths rather than isolated methods:

1. Source → compile → bytecode cache → load → execute.
2. Source change → hash mismatch → recompilation → standalone-script reload.
3. Module A requires Module B → cache hit → B changes → dependency invalidation → reload.
4. Runtime error → structured `Error` → correct source and line information.
5. Actor message → scheduler dispatch → handler execution → Future completion.
6. Bytecode cache storage round trip using an in-memory storage implementation and a temporary-file implementation.

Lunaris tests a generic `SourceProvider`/cache-storage seam with test doubles. Axiom's concrete VFS adapter and file-watcher integration must be tested in Axiom's own integration suite; Lunaris must not link against Axiom to test itself.

## 44.5 Concurrency and Safety

Concurrency tests must verify documented synchronization and ownership rules without assuming that one Luau State is safe for concurrent access. Include stress tests for Actor message queues, Future completion/cancellation races, SharedTable access, and shutdown while work is pending. Run ThreadSanitizer where supported. Run AddressSanitizer and UndefinedBehaviorSanitizer in dedicated CI configurations.

## 44.6 CI Matrix

Minimum CI coverage:

- Windows: MSVC, Debug and Release.
- Windows: MinGW GCC, at least Debug.
- Linux: GCC and Clang, Debug and Release.
- CTest execution on every supported CI build.
- Sanitizer jobs on supported Linux configurations.

Tests must also run with assertions enabled and disabled where that changes behavior. Platform-specific tests should be explicitly labeled rather than silently skipped.

## 44.7 Performance Benchmarks

GoogleTest is not a benchmark framework. Keep benchmarks separate (for example, Google Benchmark behind `LUNARIS_BUILD_BENCHMARKS`) and compare selected wrapper operations with direct Luau C API equivalents:

- State creation/destruction.
- Stack push/get/pop.
- Global lookup and assignment.
- C++ function invocation.
- Userdata method/property access.
- Script compile/load/execute with and without bytecode cache.
- Allocator profiling overhead.

Benchmarks report compiler, build type, CPU, Luau revision and benchmark configuration. Avoid hard pass/fail timing thresholds in ordinary CI; use performance baselines and investigate material regressions.

---

# 45. API Stability

The wrapper's public API should avoid exposing Luau implementation details where possible.

Architecture:

```text
Lunaris Public API
        ↓
Luau Public API
        ↓
Luau VM
```

The wrapper may use internal Luau facilities where necessary, but public headers should avoid unnecessary dependency on Luau VM internals.

---

# 46. Security and Ownership

The wrapper must define ownership clearly for:

- Lua values
- tables
- functions
- userdata
- strings
- bytecode
- script environments
- module environments

Host-provided C++ objects must not be destroyed by Luau unless ownership has explicitly been transferred.

---

# 47. Development and Release

Development:

```text
Source
 ↓
Hash
 ↓
Bytecode Cache
 ↓
Hot Reload
 ↓
VM
```

Release:

```text
AssetPack
 ↓
Precompiled Bytecode
 ↓
VM
```

Axiom can therefore optionally remove source `.luau` files from shipped builds.

---

# 48. Repository Structure

Recommended repository:

```text
Lunaris/
├── include/
│   └── lunaris/
│       ├── State.hpp
│       ├── Value.hpp
│       ├── Table.hpp
│       ├── Function.hpp
│       ├── Thread.hpp
│       ├── Userdata.hpp
│       ├── Error.hpp
│       ├── Allocator.hpp
│       ├── Compiler.hpp
│       ├── Bytecode.hpp
│       ├── BytecodeCache.hpp
│       ├── Script.hpp
│       ├── Module.hpp
│       ├── ScriptManager.hpp
│       ├── ModuleCache.hpp
│       │
│       ├── Binding/
│       │   ├── Usertype.hpp
│       │   ├── Property.hpp
│       │   ├── Constructor.hpp
│       │   ├── Overload.hpp
│       │   └── Enum.hpp
│       │
│       └── Libraries/
│           ├── Library.hpp
│           └── Threading/
│               ├── Actor.hpp
│               ├── SharedTable.hpp
│               ├── SharedTableRegistry.hpp
│               ├── Message.hpp
│               ├── Future.hpp
│               ├── TaskHandle.hpp
│               └── ExecutionContext.hpp
│
├── src/
├── tests/
│   ├── Unit/
│   ├── Integration/
│   ├── TestUtils/
│   └── data/
├── examples/
├── thirdparty/
│   ├── luau/
│   └── googletest/
├── docs/
│   └── Lunaris_Design.md
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
└── README.md
```text
Lunaris/
├── include/
│   └── lunaris/
│       ├── State.hpp
│           ├── Value.hpp
│           ├── Table.hpp
│           ├── Function.hpp
│           ├── Thread.hpp
│           ├── Userdata.hpp
│           ├── Error.hpp
│           ├── Allocator.hpp
│           ├── Compiler.hpp
│           ├── Bytecode.hpp
│           ├── BytecodeCache.hpp
│           ├── Script.hpp
│           ├── Module.hpp
│           ├── ScriptManager.hpp
│           ├── ModuleCache.hpp
│           │
│           ├── Binding/
│           │   ├── Usertype.hpp
│           │   ├── Property.hpp
│           │   ├── Constructor.hpp
│           │   ├── Overload.hpp
│           │   └── Enum.hpp
│           │
│           └── Libraries/
│               ├── Library.hpp
│               └── Threading/
│                   ├── Actor.hpp
│                   ├── SharedTable.hpp
│                   ├── SharedTableRegistry.hpp
│                   ├── Message.hpp
│                   ├── Future.hpp
│                   ├── TaskHandle.hpp
│                   └── ExecutionContext.hpp
│
├── src/
├── tests/
│   ├── Unit/
│   ├── Integration/
│   ├── TestUtils/
│   └── data/
├── examples/
├── thirdparty/
│   ├── luau/
│   └── googletest/
├── docs/
│   └── Lunaris_Design.md
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
└── README.md
```

---

# 49. Implementation Order

Each implementation step must include its corresponding GoogleTest unit tests before the next dependent feature is considered complete.

1. State
2. StateConfig
3. Custom Allocator
4. Value
5. Stack
6. Table
7. Function
8. Error Handling
9. Userdata
10. Usertype Binding
11. Properties
12. Constructors
13. Overloads
14. Enums
15. Library System
16. Compiler
17. Bytecode
18. Bytecode Cache
19. Script
20. Script Environment
21. Standalone Script Hot Reload
22. Module
23. Module Cache
24. Module Hot Reload
25. Dependency Graph
26. Actor
27. SharedTable
28. SharedTableRegistry
29. Future
30. Scheduler Integration
31. GLM Extension
32. Reflection Integration
33. `.d.luau` Generation

---

# 50. Design Principles Summary

The most important principles are:

### 1. Luau remains the VM

The wrapper is not a replacement for Luau.

### 2. The wrapper is standalone

Axiom-specific systems integrate with it but are not dependencies.

### 3. Every Luau script is reloadable

Hot reload is not restricted to modules.

### 4. Scripts and modules share infrastructure

Both use the same compiler, bytecode and cache infrastructure.

### 5. VFS belongs to Axiom

The wrapper accepts source through generic interfaces and does not depend on Axiom VFS.

### 6. Message passing is the primary Actor communication mechanism

Arbitrary cross-State function calls are not the default model.

### 7. The scheduler owns concurrency

The wrapper does not create an independent thread pool.

### 8. Sharing is explicit

Userdata sharing semantics must be declared.

### 9. Reflection is above the wrapper

Reflection can consume the wrapper, but the wrapper does not depend on Reflection.

### 10. Low-level access remains possible

The wrapper should make common operations easy without preventing advanced users from working close to the underlying Luau API.

---

# 51. Target Architecture

```text
                           Axiom
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
         VFS             Reflection         Scheduler
          │                  │                  │
          │                  ▼                  │
          │          LuauReflection            │
          │              Binder                │
          │                  │                  │
          ▼                  ▼                  ▼
      ScriptManager ─────── Lunaris ─────── Actor/Future
                             │
             ┌───────────────┼───────────────┐
             │               │               │
           State          Bytecode         Scripts
             │             Cache              │
             │               │               │
             │          Module Cache         │
             │               │               │
             └───────────────┼───────────────┘
                             ▼
                           Luau
```

The intended result is a reusable C++23 Luau embedding library that provides the ergonomics of a modern C++ binding layer while retaining direct access to Luau's underlying capabilities.
