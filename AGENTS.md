# AGENTS.md — Project Keystone

## Build & Run

```sh
cmake --preset debug && cmake --build --preset build   # configure + build
build/game/crpg                                          # run the game
ctest --preset test                                      # run tests
cmake --build --preset format                            # clang-format all sources
```

- Generator: Ninja. Binary dir: `build/`.
- All dependencies fetched via `FetchContent` (nlohmann/json, doctest, imgui, glfw, freetype, sokol, stb).
- CMake 3.28+, C++23.
- No pre-commit hooks, no CI. Lint/format is manual.

## Test

- Framework: doctest v2.5.2. Tests live in `tests/`.
- `ctest --preset test` runs the `corundum_tests` executable.
- To run a single test: `build/tests/corundum_tests -tc="*test_name*"`
- Tests link against the static `engine` library only (no platform or tools).

## Architecture

Three strict layers. Lower layers **must never** include headers from higher layers.

```
engine/                   ← Pure C++23 core. No GLFW, no ImGui, no platform headers.
engine/platform/glfw/     ← GLFW + sokol rendering. Owns main loop, window, input.
tools/                    ← ImGui editors (tilesmith, spritesmith). Dev-only.
```

`game/` contains the entry point (`main.cpp`) and data; no game logic layer exists — all logic lives in `engine/` systems.

Permitted includes:

- `engine/platform/glfw/` → `engine/`
- `tools/` → `engine/`
- `engine/` → `engine/platform/glfw/` — **never**
- `tools/` → `engine/platform/glfw/` — **never** (use abstractions)

### Lifecycle

```
initialize(App&, config_path)  → load config, sprites, font, world, dialogue
update(App&)                   → main loop: poll input → fixed update → render
cleanup(App&)                  → orderly resource teardown after loop exits
```

No virtual dispatch. `App` owns systems directly; `Scene` holds all game-world state.

### Key directories

| Path                       | Purpose                                                                       |
| -------------------------- | ----------------------------------------------------------------------------- |
| `engine/include/corundum/` | Public engine headers (namespace `corundum`)                                  |
| `engine/src/`              | Core source: core/, world/, simulation/, gameplay/, physics/, ui/, resources/ |
| `engine/src/platform/`     | GLFW + sokol platform layer (`engine_platform_glfw` target)                   |
| `game/src/`                | Entry point (`main.cpp`): creates App, calls initialize/update/cleanup        |
| `game/data/`               | JSON-driven game data: dialogue/, tilemaps/, sprite_sheets/, game.json        |
| `tests/`                   | Unit tests linked against `engine` only                                       |
| `tools/editors/`           | Tilesmith (tilemap editor) and Spritesmith (sprite editor)                    |
| `tools/platform/`          | `ToolApp` — owns GLFW window, sokol context, ImGui, texture registry          |

### Build targets

| Target                 | Type          | Notes                                                  |
| ---------------------- | ------------- | ------------------------------------------------------ |
| `engine`               | static lib    | Pure core + nlohmann/json                              |
| `engine_platform_glfw` | static lib    | GLFW + sokol render backend                            |
| `crpg`                 | executable    | Thin entry point — calls initialize → update → cleanup |
| `tilesmith`            | executable    | Tilemap editor                                         |
| `spritesmith`          | executable    | Sprite sheet editor                                    |
| `corundum_tests`       | executable    | Unit tests                                             |
| `format_code`          | custom target | Runs clang-format -i (run manually after edits)        |

## External Dependencies

### GLFW + sokol (platform/ layer only)

- GLFW is the windowing and input library. sokol_gfx is the rendering backend. Both live exclusively in `engine/platform/glfw/`.
- Never include GLFW or sokol headers outside `engine/platform/glfw/`. Engine core and tools must not see these types.
- Wrap GLFW/sokol resources in thin RAII types within the platform layer (e.g., `Window`, `SokolRenderer`).
- Translate GLFW events to engine-defined event structs (in `engine/events.hpp`) at the platform boundary. Pass only engine event types into core systems.
- All deps fetched via `FetchContent` — no `find_package` calls needed.

### ImGui (tools/ layer only)

- ImGui is used exclusively in `tools/editors/`. Never include ImGui headers in `engine/`.
- Rendering via `sokol_imgui`; initialised via `tools_imgui_backend`.
- Editor windows read from and write to engine data via explicitly passed references or `std::span` — never via globals or singletons.

### Dependency Inversion at Layer Boundaries

Pure abstract interfaces (no data members, all methods `= 0`) are the preferred tool when engine core must call into a platform implementation. `Renderer` and `Window` are the canonical examples.

- Interfaces live in `engine/` (the inner layer defines the contract).
- Platform implements; engine never knows the concrete type.
- Null/stub implementations enable unit-testing engine logic without GLFW (e.g., `NullRenderer`).
- Virtual dispatch **inside** a layer is forbidden. Use function pointers, `std::variant`, or dispatch tables for intra-layer polymorphism.

## Conventions

### C++23

- `{}` brace init preferred; avoid `=` and `()` for non-trivial types.
- `using Alias = Type;` — never `typedef`.
- `enum class` for all enums. No C-style casts.
- `std::span` for non-owning array params. `std::expected<T,E>` for fallible ops.
- **No exception classes.** All `load_*()` functions return `std::expected<T, std::string>`. Throwing is reserved for unrecoverable errors (malloc failure, etc.).
- `std::print`/`std::println` — never `printf`/`fprintf`.
- `std::format` for all string formatting.
- `std::ranges::*` algorithms preferred over raw loops. Use `std::erase_if` for filtered removal from containers.
- `[[nodiscard]]` on all functions returning handles, spans, or error values.
- `[[likely]]` / `[[unlikely]]` on hot-path branches.
- `constexpr` on all compile-time constants (never `static const`).
- `noexcept` on all hot-path functions.
- Strongly-typed handles: wrap `uint32_t` in a named struct with generation counter. Never pass raw integer IDs across API boundaries.
- No raw `new`/`delete`. Use `std::vector`, `std::unique_ptr`, `std::make_unique`.
- Use `std::atomic` (with explicit memory order) instead of `volatile` for shared state. Hot update loops are single-threaded by default.

### DOD (Data-Oriented Design)

- **No god objects** — separate data from logic entirely. Data lives in flat structs/arrays; logic lives in free functions or stateless classes.
- **SoA over AoS** — group fields by access frequency (hot/cold split). Name structs after what the data IS, never after access frequency. The words "hot", "cold", "primary", "secondary", "frequent", and "infrequent" must not appear in type names.
- **Systems touch only what they need** — each function accepts exactly the `std::span<T>` columns it reads/writes, never an entity wrapper.
- **Linear iteration only** — no virtual dispatch, no callbacks per-entity, no branching on type inside hot loops.
- `std::mdspan` for 2D+ grid data. Flat `std::vector` + `mdspan` view, never `vector<vector>`.
- Flatten nested containers into contiguous buffers with offset/count tables (e.g., `SpriteFrameIndex::frame_rects` + `anim_offsets` + `anim_frame_counts`).
- Swap-and-pop for O(1) removal from dense arrays.
- Table-based FSMs over per-entity state enums.
- Per-entity direction/enum lookups use constexpr arrays, not switch/if chains (e.g., `k_opposite_dir`, `k_anim_for_facing`, `k_facing_table`).
- No pointer chasing in hot loops; prefer contiguous arrays with index lookup.
- `alignas(64)` for cache-line alignment on hot data.
- **Class rule**: Data lives in POD structs. Logic lives in free functions (preferred) or stateless classes. Never bundle state + virtual dispatch + logic together — e.g., `RenderState` is a struct, rendering functions are free in `corundum::systems::render`.

### Naming

| Kind                 | Style                               |
| -------------------- | ----------------------------------- |
| Types                | PascalCase                          |
| Functions, variables | snake_case                          |
| Constants            | `k_snake_case`                      |
| Private members      | `snake_case_` (trailing underscore) |

- Headers: `.hpp`. Sources: `.cpp`.
- One class/struct per file for non-trivial types.

### Documentation

Use `/** ... */` Javadoc-style Doxygen comments for all public interfaces. Public interfaces are defined as:

- All types, functions, and variables in `engine/include/corundum/` (public engine API)
- Cross-subsystem exported symbols in `engine/src/`
- Platform abstractions in `engine/include/corundum/` (`Window`, `Renderer`) — concrete implementations live in `engine/src/platform/`
- Platform APIs in `engine/platform/glfw/` exposed to the engine layer

#### Mandatory tags for all public interfaces

- `@brief`: One-line summary, required for every public symbol.
- `@param`: Required for all non-obvious parameters. Use `[in]`, `[out]`, `[in,out]` qualifiers for pointer/reference parameters.
- `@return`: Required for non-void functions. Describe return values including success/error states (e.g., `std::expected` outcomes).
- `@pre`: Required for caller preconditions (e.g., thread requirements, valid pointer constraints).
- `@post`: Required for postconditions (e.g., state changes after function execution).

#### Recommended tags for game engine contexts

- `@thread_safety`: Note thread requirements (e.g., "Must be called from the render thread", "Thread-safe for concurrent reads").
- `@performance`: Document hot-path characteristics (e.g., "O(1) amortized", "Operates on SoA data for cache efficiency") for functions in physics, rendering, or entity update loops.
- `@warning`: Flag dangerous operations (e.g., "Invalidates all existing entity handles", "Do not call during frame rendering").
- `@note`: Add supplementary context (e.g., API stability, temporary status).
- `@see`: Cross-reference related types, functions, or documentation.
- `@code`/`@endcode`: Provide usage examples for complex APIs.

#### Internal documentation

Use `///` triple-slash Doxygen comments for non-public but non-trivial functions in `engine/src/`. Brief descriptions only, no mandatory tags unless needed.

#### Formatting rules

- Comments must precede the symbol declaration (headers) or definition (source files without header declarations).
- Avoid redundant comments that repeat information already clear from the function signature.
- Use Doxygen-flavored markdown for lists, code blocks, and cross-references.

### Formatting

- `.clang-format`: LLVM-based, 2-space indent, 120 col limit, `Attach` braces.
- Run via `cmake --build --preset format`.

### Linting

- `.clang-tidy`: bugprone, performance, portability, readability, modernize, cppcoreguidelines, clang-analyzer, misc.
- `WarningsAsErrors`: bugprone, clang-analyzer, cppcoreguidelines.

### Error Handling

- No exceptions in hot paths.
- `std::expected<T, E>` for fallible factory functions and resource acquisition.
- `assert()` (debug-only) for invariant checks inside hot paths.

## Platform Notes

- Windowing uses **GLFW**; rendering uses **sokol**. The platform layer wraps both behind `Window` and `Renderer`.
- macOS: Metal backend (`SOKOL_METAL`). Windows: D3D11. Linux: GLCore (all via sokol).
- ImGui tools use `sokol_imgui` for rendering.
- Tools compile with `tools_imgui_backend` (shared ImGui + sokol_imgui + GLFW).
- On macOS, `simgui_impl.cpp` compiles as OBJCXX.

## Game Data

- All game config, dialogue, tilemaps, and sprite sheets are JSON/data-driven in `game/data/`.
- Dialogue: directed-graph format in `game/data/dialogue/` — see `docs/dialogue.md`.
- Main config: `game/data/game.json` — see `docs/game_config.md`.
- Config path is passed to `initialize` in `main.cpp`.

## Key Data Structures

- `RenderState` (`engine/include/corundum/render_state.hpp`) — all mutable rendering state (sprite index, chunks, collision aggregates, dialog box). Operated on by free functions in `corundum::systems::render`.
- `Renderer` (`engine/include/corundum/renderer.hpp`) — concrete struct of function pointers with `void* state`. No virtual dispatch. Created by `make_sokol_renderer()` or `make_null_renderer()`.
- `DialogBoxState` (`engine/include/corundum/ui/dialog_box.hpp`) — POD struct operated on by `dialog_box_update()` / `dialog_box_render()` free functions.
- `ToolTexture` (`tools/editors/common/tool_texture.hpp`) — opaque `uint32_t id` + `w`/`h`. All sokol texture ops live in `ToolApp`.

## What NOT to Do

- No virtual functions in performance-critical data systems. Use function pointers, `std::variant + std::visit`, or dispatch tables instead.
- No `std::list` or other node-based containers in hot paths.
- No `std::shared_ptr` in update loops (reference count overhead).
- No C headers (`<stdio.h>`, `<stdlib.h>`, etc.) — use `<cstdio>`, `<cstdlib>`, or C++ equivalents.
- No `typedef` — use `using`.
- No GLFW or sokol headers outside `engine/platform/glfw/`.
- No ImGui headers outside `tools/editors/`.
