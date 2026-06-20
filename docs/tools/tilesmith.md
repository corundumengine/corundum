# tilesmith

`tilesmith` is a visual tilemap editor for maps in the engine's native JSON format. It reads and writes the same files used at runtime — no conversion step required.

---

## Building

```
cmake --build build --target tilesmith
```

The binary is written to `build/tilesmith`.

---

## Running

Run from the **project root** so that relative asset paths (`data/`, `assets/`) resolve correctly:

```
./build/tilesmith <map.json>
```

Example:

```
./build/tilesmith game/data/tilemaps/world.json
```

## UI Layout

```
┌─────────────────────────────────────┬──────────────┐
│                                     │  Layer strip │
│           Canvas                    │──────────────│
│      (scrollable map view)          │              │
│                                     │  Tile grid   │
│                                     │  (palette)   │
│                                     │              │
├─────────────────────────────────────┴──────────────┤
│  Status bar                                        │
└────────────────────────────────────────────────────┘
```

| Region                                   | Description                                                                                                                                               |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Canvas** (left, 1080×744)              | Displays all tilemap layers. Scroll with arrow keys or middle-mouse drag.                                                                                 |
| **Layer strip** (top of right panel)     | One row per layer. Click a row to make it active. Active layer is highlighted.                                                                            |
| **Tile grid** (remainder of right panel) | All tiles in the current tileset, laid out in a grid. Click to select the tile to paint. Scroll with the mouse wheel. Selected tile has a yellow outline. |
| **Status bar** (bottom)                  | Shows active layer name, selected tile GID, and a `*` when there are unsaved changes.                                                                     |

---

## Controls

### Navigation

| Input             | Action                            |
| ----------------- | --------------------------------- |
| Arrow keys        | Pan the canvas one tile at a time |
| Middle mouse drag | Free pan the canvas               |

Camera is clamped to map bounds — you cannot scroll past the edge of the map.

### Editing

| Input                | Action                                                        |
| -------------------- | ------------------------------------------------------------- |
| Left click (canvas)  | Paint the selected tile at the cursor position                |
| Left drag (canvas)   | Drag-paint across multiple tiles                              |
| Right click (canvas) | Erase the tile under the cursor                               |
| Right drag (canvas)  | Drag to define a rect; releases erases all tiles in that area |

Right-click drag shows a red-orange preview rectangle while dragging. The erase commits when you release the mouse button — tiles are not erased mid-drag.

Painting and erasing affect only the **active layer**. Other layers are visible but not modified.

### Layer selection

| Input             | Action                                    |
| ----------------- | ----------------------------------------- |
| Click a layer row | Make that layer active                    |
| `Tab`             | Cycle to the next layer                   |
| `1` – `9`         | Jump directly to layer by index (1-based) |

### Tile selection

| Input                   | Action                     |
| ----------------------- | -------------------------- |
| Left click (tile grid)  | Select the tile to paint   |
| Mouse wheel (tile grid) | Scroll through the tileset |

### Collision overlay

| Input              | Action                                                  |
| ------------------ | ------------------------------------------------------- |
| `C`                | Toggle collision overlay (enters rect mode by default)  |
| `T`                | Toggle between rect mode and triangle mode (overlay on) |
| `[` / `]`          | Cycle triangle orientation (triangle mode only)         |

While the collision overlay is active:

**Rect mode** (default):

- **Left-click + drag** draws a new collision rectangle, snapped to the tile grid. A green preview tracks the drag; committed on release.
- **Shift + left-click + drag** draws a rectangle with pixel precision — not snapped to the tile grid. Use this for thin barriers such as ledge edges (e.g. a 4-pixel-tall strip along a platform edge).
- **Right-click** on an existing rectangle deletes it. If rectangles overlap, the topmost one is deleted.

**Triangle mode** (`T` to enter):

Each triangle occupies exactly one tile and is a right-isoceles half-tile shape. The four orientations name the **solid** half:

| Key     | Orientation | Solid region                              |
| ------- | ----------- | ----------------------------------------- |
| default | `NW`        | Upper-left — hypotenuse runs TR → BL      |
| `]`     | `NE`        | Upper-right — hypotenuse runs TL → BR     |
| `]`     | `SE`        | Lower-right — hypotenuse runs TR → BL     |
| `]`     | `SW`        | Lower-left — hypotenuse runs TL → BR      |

The `[` key cycles backwards through the same order.

A green triangle cursor previews the current orientation while hovering. Placed triangles appear orange.

- **Left-click** places a triangle at the hovered tile. Placing the same orientation on the same tile twice has no effect.
- **Right-click** on a tile removes the triangle whose bounding rect contains the click.

Tile painting and erasing are suspended while the collision overlay is active.

### File

| Input           | Action                                                |
| --------------- | ----------------------------------------------------- |
| `Ctrl+S`        | Save the map to disk                                  |
| `Escape` or `Q` | Quit (no save prompt — unsaved changes are discarded) |

---

## Workflow

### Typical session

1. Open the map in `tilesmith`:
   ```
   ./build/tilesmith game/data/tilemaps/world.json
   ```
2. Select a layer in the layer strip.
3. Click a tile in the palette to select it.
4. Left-click or drag on the canvas to paint.
5. Right-click or drag on the canvas to erase one tile or a rectangular area.
6. Press `Ctrl+S` to save.

### Multiple layers

The layer strip lists all layers in the map, bottom-to-top in the same order the renderer draws them. `z_index = 0` layers (ground, decorations) appear below entities at runtime; layers with `z_index > 0` (canopies, pillar tops) appear above.

Switch between layers freely during a session. Each `Ctrl+S` saves all layers regardless of which is active.

### Editing collision shapes

1. Press `C` to enter collision overlay mode (starts in rect mode).
2. **To add a rectangle:** Left-click and drag to define a new collision rectangle snapped to the tile grid. Release to commit. Hold `Shift` while dragging to use pixel-precision sizing instead of tile snapping — useful for thin ledge barriers.
3. **To add a triangle:** Press `T` to switch to triangle mode. Use `[` / `]` to pick the orientation. Left-click a tile to place a half-tile triangle there.
4. **To delete:** Right-click on a collision shape to remove it. In rect mode, the click must land inside a rectangle. In triangle mode, the click must land inside the bounding box of the triangle you want to remove.
5. Press `C` again to return to tile-editing mode.

Overlapping collision rectangles are not allowed — a new rectangle that would overlap any existing one is silently discarded. Triangles have no overlap restriction.

Press `Ctrl+S` to save.

### After saving

`tilesmith` writes the map back to the same `.json` file it loaded. The format is identical to the engine's native tilemap format — the game loads it without any additional steps.

Layer format (dense vs. sparse) is re-evaluated per layer on save using a 60% empty-tile threshold. A layer that was dense on load may become sparse if enough tiles were erased, and vice versa. This is cosmetic: both formats are semantically equivalent.

---

## Known limitations

- **No undo.** `Ctrl+Z` has no effect. Save frequently with `Ctrl+S`.
- **No save prompt on exit.** Pressing `Escape` or closing the window discards any unsaved changes silently. The `*` in the status bar is the only indicator of unsaved state.
- **One tileset palette at a time.** The palette shows one tileset at a time. Tiles from other tilesets that are already on the map remain visible, but you can only paint from the tileset shown in the palette.
- **Collision shapes can be added or deleted but not moved.** To reposition a shape, delete it and redraw.
- **Triangle orientation is set before placement.** To change the orientation of an existing triangle, delete it and place a new one with the desired orientation.
