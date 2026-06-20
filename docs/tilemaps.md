# Tilemaps

Tilemaps define the visual world — the floors, walls, and decorations that entities move through. Everything is declared in JSON; no source code changes are needed to add or modify a map.

## Concepts

### Tilesets

A **tileset** is a PNG image containing a grid of uniformly-sized tiles. A map can reference **multiple tilesets**. Each tileset is assigned a `first_gid` (Global ID); tiles from that tileset occupy the GID range `[first_gid, first_gid + tile_count)`. Local tile position within the sheet is computed as `local_id = gid - first_gid`, then `col = local_id % columns`, `row = local_id / columns`.

All tilesets in a map must use the same tile dimensions (e.g., all 16×16).

### Tilemaps

A **tilemap** is a fixed-size grid of GIDs. At the default 2× scale, 16×16 pixel tiles render as 32×32 screen pixels.

### Layers

A tilemap has one or more **layers**. Empty cells in a layer are transparent, so lower layers show through. Each layer has an optional `"z_index"` field that controls its depth relative to entities:

| `z_index`     | Draw order                                          |
| ------------- | --------------------------------------------------- |
| `0` (default) | Before entities — ground, water, object bases       |
| `1`           | After all entities — canopies, tops of tall objects |
| `2`, `3`…     | After z=1 layers, in ascending order                |

Negative values are clamped to `0` by the loader. The engine computes the set of above-entity z-values once at startup and iterates them each frame after entity rendering.

Entity rendering is **y-sorted**: entities are drawn in ascending order of `position.y + frame_height * SPRITE_SCALE` (their bottom edge), so a character further south always renders on top of one further north.

Each layer also uses one of two tile formats:

| Format | Use when               | JSON key    |
| ------ | ---------------------- | ----------- |
| Dense  | Most cells have a tile | `"tiles"`   |
| Sparse | Most cells are empty   | `"objects"` |

---

## Adding a tilemap

### 1. Prepare tileset PNGs

Place each PNG in `game/assets/textures/`. Tiles must be uniformly sized with no margin or spacing between them. All tilesets used by one map must share the same tile dimensions.

### 2. Create tileset JSON files

Create a `.json` file in `game/data/tilemaps/` for each tileset:

```json
{
  "path": "game/assets/textures/my_tileset.png",
  "tile_width": 16,
  "tile_height": 16,
  "columns": 8,
  "rows": 4
}
```

### 3. Create a tilemap JSON file

Create another `.json` file in `game/data/tilemaps/`. List all tilesets in the `"tilesets"` array. Each entry needs a `first_gid` and a `source` path. The first tileset must have `first_gid: 0`; subsequent tilesets must start exactly where the previous one ends (no gaps, no overlaps).

```json
{
  "id": "my_map",
  "tilesets": [
    { "first_gid": 0, "source": "game/data/tilemaps/tileset_a.json" },
    { "first_gid": 32, "source": "game/data/tilemaps/tileset_b.json" }
  ],
  "width": 20,
  "height": 15,
  "layers": [
    {
      "name": "ground",
      "tiles": [
        "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1",
        "1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1",
        "1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1",
        "1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1"
      ]
    },
    {
      "name": "objects",
      "objects": [
        { "col": 5, "row": 1, "id": 3 },
        { "col": 12, "row": 2, "id": 33 }
      ]
    },
    {
      "name": "canopy",
      "z_index": 1,
      "objects": [{ "col": 5, "row": 0, "id": 10 }]
    }
  ]
}
```

In the example above, tile ID 3 is local tile 3 of tileset A; tile ID 33 is local tile 1 of tileset B (`33 - 32 = 1`).

### 4. Add collision rects (optional)

To mark areas as impassable, add a `"collisions"` array to the tilemap JSON. Each entry is a rectangle in **Tiled pixel coordinates** (unscaled). The engine scales them to world pixels at startup.

```json
{
  "id": "my_map",
  "collisions": [
    { "x": 64.0, "y": 0.0,  "w": 32.0, "h": 128.0 },
    { "x": 0.0,  "y": 96.0, "w": 640.0, "h": 16.0  }
  ],
  ...
}
```

The easiest workflow is to draw rectangle objects on an object layer in Tiled, then re-run `import_tiled` — it extracts all rectangles from every object layer and writes the `"collisions"` array automatically. Non-rectangle objects (polygons, ellipses) are skipped with a warning.

Collision uses axis-separated AABB resolution: the player can slide along a wall when moving diagonally into it.

### 5. Add actors (optional)

To spawn NPCs on the map, create `data/spawn_points/<map_name>.json`. See [spawn_points.md](spawn_points.md) for the format.

### 6. Size the map for scrolling

The camera centers on the player and clamps to map bounds. The player position is also clamped to the map bounds each frame, so the player cannot walk off any edge. A map **smaller** than the window (800×600) locks the camera at `{0, 0}` — valid, but the viewport won't scroll. For scrolling to activate, the map must exceed the window in at least one dimension.

At the default 2× tile scale with 16×16 tiles:

| Map size (tiles) | World size (px) | Scrolls?                  |
| ---------------- | --------------- | ------------------------- |
| 20 × 15          | 640 × 480       | No — smaller than 800×600 |
| 40 × 30          | 1280 × 960      | Yes                       |
| 100 × 100        | 3200 × 3200     | Yes                       |

### 7. Load the map in `main.cpp`

Pass the path to `load_tilemap`, then load all tileset textures via `load_tilemap_textures`:

Then call `render_tilemap` in the game loop. The `z_index` parameter selects which layers to draw. The `above_z` vector (computed at startup) holds all z-values > 0 found in the map, sorted ascending:

The camera is updated automatically — no additional renderer changes are needed.

---

## Layer formats

### Dense (`"tiles"`)

An array of `height` strings. Each string is a comma-separated row of exactly `width` GIDs. One string per row — formatters will not reflow the content of a string, so the grid structure is preserved in the file regardless of editor settings.

```json
"tiles": [
  "1,1,1,1,1,1,1,1",
  "1,0,0,0,0,0,0,1",
  "1,0,0,0,0,0,0,1",
  "1,1,1,1,1,1,1,1"
]
```

Use this format when most cells in a layer have a tile. For layers that are mostly empty (decorations, objects), use the sparse format instead.

### Sparse (`"objects"`)

An array of positioned tile entries. All cells not listed are implicitly empty.

```json
"objects": [
  { "col": 3, "row": 2, "id": 7 },
  { "col": 8, "row": 5, "id": 33 }
]
```

`id` values are GIDs — they must fall within a tileset's range. The loader validates that no two entries share the same `(col, row)` position.

---

## Reference

### Tileset JSON fields

| Field         | Required | Type   | Description                                         |
| ------------- | -------- | ------ | --------------------------------------------------- |
| `path`        | ✓        | string | Path to the PNG, relative to the working directory. |
| `tile_width`  | ✓        | int    | Width of one tile in pixels.                        |
| `tile_height` | ✓        | int    | Height of one tile in pixels.                       |
| `columns`     | ✓        | int    | Number of tile columns in the sheet.                |
| `rows`        | ✓        | int    | Number of tile rows in the sheet.                   |

### Tilemap JSON fields

| Field        | Required | Type   | Description                                                                |
| ------------ | -------- | ------ | -------------------------------------------------------------------------- |
| `id`         | ✓        | string | Unique map identifier.                                                     |
| `tilesets`   | ✓        | array  | At least one tileset entry (see below).                                    |
| `width`      | ✓        | int    | Map width in tiles.                                                        |
| `height`     | ✓        | int    | Map height in tiles.                                                       |
| `layers`     | ✓        | array  | At least one layer object (see below).                                     |
| `collisions` |          | array  | Impassable rectangles in Tiled pixel space. Omit if no collision geometry. |

### Tileset entry fields (inside `"tilesets"`)

| Field       | Required | Type   | Description                                                          |
| ----------- | -------- | ------ | -------------------------------------------------------------------- |
| `first_gid` | ✓        | int    | First GID assigned to this tileset. Must be `0` for the first entry. |
| `source`    | ✓        | string | Path to the tileset JSON file.                                       |

Tilesets must be contiguous: `tilesets[i+1].first_gid == tilesets[i].first_gid + tilesets[i].tile_count`. Gaps and overlaps both throw `TilemapLoadError`.

### Layer fields

| Field     | Required         | Type   | Description                                                                                                                                             |
| --------- | ---------------- | ------ | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `name`    | ✓                | string | Layer name. Used for identification; no rendering significance.                                                                                         |
| `tiles`   | ✓ (or `objects`) | array  | Dense format: array of `height` CSV row strings.                                                                                                        |
| `objects` | ✓ (or `tiles`)   | array  | Sparse format: array of `{col, row, id}` objects.                                                                                                       |
| `z_index` |                  | int    | Depth relative to entities. `0` = below entities (default). Positive values draw after entities in ascending order. Negative values are clamped to `0`. |

Exactly one of `tiles` or `objects` must be present on each layer.

### Sparse object fields

| Field | Required | Type | Description                                      |
| ----- | -------- | ---- | ------------------------------------------------ |
| `col` | ✓        | int  | Zero-based tile column. Must be in `[0, width)`. |
| `row` | ✓        | int  | Zero-based tile row. Must be in `[0, height)`.   |
| `id`  | ✓        | int  | GID. Must be covered by a tileset in the map.    |

### Collision rect fields (inside `"collisions"`)

Coordinates are in **Tiled pixel space** (unscaled). The engine multiplies all values by `TILE_SCALE` at startup.

| Field | Required | Type  | Description                                 |
| ----- | -------- | ----- | ------------------------------------------- |
| `x`   | ✓        | float | Left edge of the rectangle in Tiled pixels. |
| `y`   | ✓        | float | Top edge of the rectangle in Tiled pixels.  |
| `w`   | ✓        | float | Width in pixels. Must be positive.          |
| `h`   | ✓        | float | Height in pixels. Must be positive.         |
