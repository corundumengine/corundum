# Sprites

Sprites are defined entirely in JSON — no source code changes are needed to add a character or animation. The engine loads all sheet definitions at startup and maps sprite names to texture regions at render time.

## Concepts

### Sprite sheets

All sprites come from **sprite sheets** — a single PNG image containing multiple frames arranged in a grid. Each sheet has a fixed grid cell size (`frame_width` × `frame_height`). Frames are addressed by `(col, row)` zero-based grid coordinates.

### Multi-tile sprites

By default each sprite occupies one grid cell. Characters that are taller or wider than one tile use `col_span` and `row_span` on the sprite object to declare how many grid cells they occupy. The rendered source rectangle is computed as:

- **width** = `col_span × frame_width + (col_span − 1) × spacing_x`
- **height** = `row_span × frame_height + (row_span − 1) × spacing_y`

The `(col, row)` frame coordinate always points to the **top-left cell** of the sprite. The engine extends the source rect down and to the right automatically.

Common sizes:

| Layout           | `col_span` | `row_span` |
| ---------------- | ---------- | ---------- |
| 1 tile (default) | 1          | 1          |
| 2 tiles tall     | 1          | 2          |
| 2×2 tiles        | 2          | 2          |
| 2×3 tiles        | 2          | 3          |

### Sprites and animations

A single sheet file can define multiple named **sprites** (e.g. `"player"`, `"innkeeper"`). Each sprite has one or more named **animations** (e.g. `"idle"`, `"walk_south"`). Each animation is an ordered list of frame coordinates that the engine cycles through.

A single-frame animation is a static pose — just put one entry in the array.

### Scale

All sprites are rendered at 2× scale.

---

## Adding a sprite

### 1. Add the PNG

Place the sprite sheet PNG in `game/assets/textures/`. Frame dimensions must be uniform across the entire sheet.

### 2. Create a sheet JSON file

Create a `.json` file in `data/textures/`. It can live alongside existing sheet files — each file defines one sheet and any number of sprites.

```json
{
  "id": "guards",
  "path": "game/assets/textures/guards.png",
  "frame_width": 16,
  "frame_height": 16,
  "frames": {
    "guard": {
      "col_span": 1,
      "row_span": 2,
      "walk_around_offset": 0.8,
      "idle": [{ "col": 1, "row": 0 }],
      "walk_south": [
        { "col": 0, "row": 0 },
        { "col": 1, "row": 0 },
        { "col": 2, "row": 0 }
      ],
      "walk_north": [
        { "col": 0, "row": 9 },
        { "col": 1, "row": 9 },
        { "col": 2, "row": 9 }
      ],
      "walk_east": [
        { "col": 0, "row": 6 },
        { "col": 1, "row": 6 },
        { "col": 2, "row": 6 }
      ],
      "walk_west": [
        { "col": 0, "row": 3 },
        { "col": 1, "row": 3 },
        { "col": 2, "row": 3 }
      ]
    }
  }
}
```

Each `(col, row)` points to the **top** tile of the character. With `row_span: 2` and `frame_height: 16`, the engine captures 32 px of height from that row downward.

---

## Animated sprites

To animate a sprite, it must have an `Animation` component alongside its `Sprite` component. The engine advances the frame index automatically based on elapsed time and `frame_duration`.

Static sprites (NPCs that don't walk) only need a `Sprite` component — no `Animation` needed.

---

## Reference

### Sheet definition fields

| Field          | Required | Type   | Description                                                                           |
| -------------- | -------- | ------ | ------------------------------------------------------------------------------------- |
| `id`           | ✓        | string | Unique identifier for this sheet. Not referenced directly in code.                    |
| `path`         | ✓        | string | Path to the PNG, relative to the working directory.                                   |
| `frame_width`  | ✓        | int    | Width of one frame in pixels.                                                         |
| `frame_height` | ✓        | int    | Height of one frame in pixels.                                                        |
| `offset_x`     |          | int    | Pixel offset from the left edge of the image to the first frame column. Default: `0`. |
| `offset_y`     |          | int    | Pixel offset from the top edge of the image to the first frame row. Default: `0`.     |
| `spacing_x`    |          | int    | Horizontal gap in pixels between frame columns. Default: `0`.                         |
| `spacing_y`    |          | int    | Vertical gap in pixels between frame rows. Default: `0`.                              |
| `frames`       | ✓        | object | Map of sprite name → sprite object (see below).                                       |

### Sprite fields

Each entry in `frames` is an object with optional sprite-level fields alongside the animation entries:

| Field                | Required | Type  | Description                                                                                                                                                                                                                   |
| -------------------- | -------- | ----- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `col_span`           |          | int   | Number of grid columns the sprite occupies. Default: `1`.                                                                                                                                                                     |
| `row_span`           |          | int   | Number of grid rows the sprite occupies. Default: `1`.                                                                                                                                                                        |
| `walk_around_offset` |          | float | Fraction of the sprite height (0–1) defining where the feet are. The lower portion (from this offset to the bottom) is the collision hitbox, allowing other entities to walk in front of or behind this sprite. Default: `0`. |

### Animation map

Each remaining key in a sprite entry maps an animation name → array of frame coordinates:

```json
"animation_name": [
  { "col": 0, "row": 0 },
  { "col": 1, "row": 0 }
]
```

| Field | Required | Description                     |
| ----- | -------- | ------------------------------- |
| `col` | ✓        | Zero-based column of the frame. |
| `row` | ✓        | Zero-based row of the frame.    |

Frames are played in array order and loop. A single-element array is a static pose.
