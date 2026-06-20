# Glossary

This glossary defines the canonical terms used throughout the codebase.
When a term appears in code, data files, or documentation it should match
the definition here exactly. Prefer these terms in code review, discussion,
and new features.

---

## Sprite

A named, animated game entity visual — for example `"player"` or
`"innkeeper"`. Identified at runtime by a `sprite_name` string. A sprite
belongs to exactly one sprite sheet and is defined by one or more named
animations (sequences of `FrameCoord` values).

_Core types:_ `core::ecs::Sprite` component, `core::assets::Frames`

---

## Sprite Sheet

A PNG image that contains every animation frame for one or more sprites
packed into a grid, together with a companion JSON metadata file that
describes the grid layout and the animations each sprite exposes.

At the data level a sprite sheet has two parts:

- **Sheet** — the image layout (`SpriteSheet`: path, frame size, offsets,
  spacing). Identified by a `Id`.
- **Sprite entries** — one or more named sprites with their animation
  frame sequences (`Frames`).

_Core types:_ `core::assets::SpriteSheet`, `core::assets::Frames`,
`core::assets::Id`
_Data:_ `game/data/sprite_sheets/<name>.json`

---

## Sheet

Short identifier form for _sprite sheet_, used in type and variable names
(`Id`, `sheet_id`, `NULL_SHEET`). Refers specifically to the image
layout half of a sprite sheet (as opposed to the sprite entries it
contains).

---

## CharacterRegistry

The core (`core::assets`) registry responsible for loading sprite sheet
metadata from JSON and providing lookups by sprite name or sheet ID.
Has no GLFW or other platform dependency — pure C++23 standard library.

_Core type:_ `core::assets::CharacterRegistry`

---

## Tileset

A PNG image of tile graphics used by the tilemap system, described by a
tileset JSON file. Distinct from a sprite sheet: tilesets are world
geometry; sprites are animated entities. A tileset has no named animations
— tiles are addressed by a numeric Global ID (GID).

_Core types:_ `core::tilemap::TilemapTileset`, `core::tilemap::TilesetInfo`
_Data:_ `game/data/sprite_sheets/<mapid>_tileset_N.json`

---

## Tilemap

A fixed-size grid of tile IDs referencing one or more tilesets, used as
world geometry. Not an ECS entity — passed directly to the renderer and
the collision system.

_Core types:_ `core::tilemap::Tilemap`, `core::tilemap::TilemapLayer`
_Data:_ `game/data/tilemaps/<name>.json`

---

## Placement

A named spawn point in a companion JSON file that maps tile-grid
coordinates to an entity type (`"player"` or `"npc"`), a sprite name,
and an optional dialogue graph reference. Tile coordinates are converted
to world pixels at startup.

_Core types:_ `core::game::SpawnPoint`, `core::game::PlacementLoadError`
_Data:_ `game/data/tilemaps/<mapid>_placements.json`

---

## GID (Global ID)

A numeric tile identifier used in the tilemap layer data. GIDs are
zero-based and span all tilesets in a map contiguously. `0xFFFF` is the
reserved `EMPTY_TILE` sentinel (transparent, never rendered).
`local_id = gid - tileset.first_gid` gives the tile's position within
its owning tileset.

---

## Dialogue Graph

A directed graph of conversation nodes loaded from JSON. Nodes are of
type `Text` (the NPC speaks) or `Choice` (the player selects an option).
Edges carry optional flag conditions and set-flag effects. A reserved
`"end"` target closes the dialogue.

_Core types:_ `core::game::DialogueGraph`, `core::game::DialogueNode`,
`core::game::DialogueState`
_Data:_ `game/data/dialogue/<graph_id>.json`

---

## Flag

A named integer in the `FlagStore` (`std::unordered_map<std::string, int>`)
that persists across dialogue interactions. Dialogue edges can require a
flag, forbid a flag, or set a flag when taken — enabling branching
without hard-coded C++ state.

_Core type:_ `core::game::FlagStore`

---

## Action

An abstract input intent consumed by core systems — for example
`MoveUp`, `Select`, `Quit`. GLFW key bindings are translated to `Action`
values in the platform layer; `core/` never sees raw key codes.

_Core type:_ `core::input::Action`
