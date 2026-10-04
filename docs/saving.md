# Saving

Corundum's save system writes player state to a versioned JSON file. There's no auto-save. Your game calls `save_game()` when it's time to save.

## Overview

Two pieces do the work:

1. **`SaveState`:** a struct that holds everything being saved
2. **`corundum::save::save_game()` / `load_game()`:** the functions that write and read it

Save files carry a version. A file whose version is not the engine's current version is refused, not migrated — pre-1.0 Corundum makes no backward-compatibility promise, so a format change means bumping the version and letting old saves fail.

## SaveState

```cpp
struct SaveState {
    int version = k_save_version;      // On-disk format version
    std::string game_id;               // From game.json; guards cross-game loads
    std::string mode;                  // "single_map" | "world"
    std::string map_or_world_id;       // Tilemap path (single_map) or world manifest path (world)
    std::string active_zone;           // Scene zone id at save time
    float player_col = 0.f;            // Player tile column
    float player_row = 0.f;            // Player tile row
    bool entered_from_world = false;   // Return-journey marker across interiors
    corundum::world::FlagStore flags;  // Global flags, verbatim
};
```

### Flags

The whole global `FlagStore` is stored as a `{ "key": int }` object, key for key. That includes every namespaced key the engine uses:

- `quest.<id>` and `quest.<id>.seen.<stage>`: quest progress
- `zone.<id>.<key>`: per-zone local state (from `local.<key>` in dialogue)
- `npc.<id>.<key>`: per-NPC state
- `item.<id>`: item counts
- `rep.<id>`: reputation
- `_visit_<graph>_<node>` / `_once_<graph>_<node>_<edge>` / `_node_once_<graph>_<node>`: internal dialogue sequencing state

Absent keys aren't written at all; on load, a missing flag reads as `0`.

## Save and Load

```cpp
#include <corundum/save/save.hpp>

// Save the engine's current state to a file
std::filesystem::path save_path("saves/save_game.json");
auto result = corundum::save::save_game(engine, save_path);

// Load a save back into the engine
result = corundum::save::load_game(engine, save_path);
```

Both return `std::expected<void, std::string>`: `ok` on success, or an error string saying what went wrong.

### Saving

```cpp
corundum::save::save_game(
    engine,   // The initialized engine to snapshot
    save_path // Destination path
);
```

This captures the render mode, active map/world, zone, player position, return-journey marker, and the whole flag store. An existing file is overwritten. It errors if there's no active map (in single-map mode) or the file can't be written.

The engine never calls `save_game()` on its own. Your game picks the moment: a menu action, a checkpoint, quitting.

### Loading

```cpp
corundum::save::load_game(
    engine,   // The initialized engine to overwrite
    save_path // Source path
);
```

It rebuilds the scene at the saved location through the transition machinery (`enter_world` for world mode, `load_map` for single-map mode) and only then restores the saved flags and return-journey marker, so a failed load leaves the running game untouched. NPCs respawn from their spawn-point data; any per-NPC state you keep in `npc.<id>.*` flags comes back verbatim along with everything else (the engine doesn't reconstruct NPC state from those flags by itself).

## Versioning

Save files carry an integer `version` (currently `1`). On load, the version must equal the engine's `k_save_version`; anything else — older or newer — fails with a clear message. The absent-field form reads as version 1.

There is deliberately no migration chain before 1.0: when the save format changes, bump `k_save_version` and let old saves be refused. See `prepare_schema_version`'s note in `core/schema_version.hpp` for the same policy on asset documents.

## Unknown Keys (Forward Compatibility)

The engine preserves unknown flag keys. Because the flag store is serialized verbatim, any flag your game set, including ones the engine doesn't recognise, survives a save/load round-trip unchanged.

That's what lets you add new flags in an update without breaking old saves. Document your custom keys so future authors know what they mean.

## Load Behavior

On load, the engine:

1. Reads and parses the save JSON
2. Validates the fields and the version
3. Checks the save's `game_id` matches the running game, and (in world mode) that its world manifest matches (a mismatch is an error)
4. Rebuilds the scene at the saved location via the transition machinery (`enter_world` for world mode, `load_map` for single-map mode)
5. Replaces the engine's flags with the saved flags and restores the active zone and the return-journey marker

## Loading Errors

Loading can fail in a few ways, each returning an error string:

- File not found or unreadable
- Malformed JSON, or a field with the wrong JSON type
- Save `mode` is neither `single_map` nor `world`
- Save version differs from the engine's
- Save `game_id` differs from the running game, or (in world mode) the world manifest differs
- The map/world can't be loaded at the saved spawn point

Handle it gracefully:

```cpp
auto result = corundum::save::load_game(engine, save_path);
if (!result) {
    // result.error() is a human-readable string describing why loading failed
    show_message(result.error());
}
```

## Example

```cpp
#include <print>

#include <corundum/engine.hpp>
#include <corundum/save/save.hpp>

void save_progress(Engine& engine) {
    auto result = corundum::save::save_game(engine, "saves/slot_1.json");
    if (!result)
        std::println(stderr, "save failed: {}", result.error());
}

void load_progress(Engine& engine) {
    auto result = corundum::save::load_game(engine, "saves/slot_1.json");
    if (!result)
        std::println(stderr, "load failed: {}", result.error());
}
```

## Checklist

When adding save functionality:

- [ ] Call `save_game()` explicitly in game code (no auto-save)
- [ ] Link `corundum::engine` (the save module compiles into the engine static lib, not a separate target)
- [ ] Document your custom save keys
- [ ] Test loading your save with the current engine version
- [ ] Test saving from the current engine version
- [ ] Bump `k_save_version` before shipping a format change (old saves are refused, not migrated)
