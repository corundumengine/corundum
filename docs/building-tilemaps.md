# Building Tilemaps

Tilemaps are authored in **Tilesmith** (`build/tools/tilesmith`) and saved as
JSON files in `data/tilemaps/`. This guide covers the editor workflow end to
end — creating a map, painting layers, elevation, ramps, collisions, and
portals — plus the on-disk JSON format each of those produces.

---

## Launching Tilesmith

```sh
build/tools/tilesmith                    # opens the "New Tilemap" dialog
build/tools/tilesmith data/tilemaps/foo.json   # opens an existing map
```

### The New Tilemap dialog

| Field | Meaning |
|---|---|
| Map ID | Filename (without `.json`), letters/digits/`_`/`-` only. Written to `data/tilemaps/{id}.json`. |
| Width / Height | Map size in tiles. Defaults to 60×45 — big enough that panning/scrolling actually matters, small enough to hand-author without autotiling. |
| Diamond W / Diamond H | The isometric tile *footprint* in pixels (the grid step), independent of the sprite art's pixel size. Defaults 128×64 (2:1 ratio). |
| Initial layer name | Name of the first (ground, z_index 0) layer. |
| Tileset source | Path to a tileset JSON (e.g. `data/sprite_sheets/objects/terrain.json`), relative to the project root. |

---

## Layers

The palette panel's layer strip (top-left) lists every layer. `+`/`-` add or
delete a layer; double-click a row to rename; click the eye icon to toggle
visibility; click a row to make it the active layer (or press `Tab` to cycle,
or `1`–`9` to jump directly).

`z_index` controls draw order relative to entities: `0` (the default for new
layers) draws below entities — this is the **ground layer**, and it's the
only kind of layer elevation, walkability, and ramps operate on. `z_index >
0` layers always draw above entities (rooftops, canopy, decals) and never
participate in elevation/walkability.

A cell can have independently-painted data on more than one z_index==0 layer;
elevation, material, and ramp lookups all resolve to the **topmost layer with
a tile present at that cell** — paint order across layers matters.

---

## Painting tiles

Default mode (no overlay toggled). Select a tile in the palette panel (right
side) — click a tile, or scroll with the mouse wheel to see more of the
tileset. `X`/`Y` toggle horizontal/flip vertical flip for the next paint.

- **Left click / drag**: paint the selected tile.
- **Right click / drag**: erase (drag to erase a rectangle).
- **`F`**: fill every *empty* cell of the active layer with the selected
  tile — only works on a z_index==0 (ground) layer; shows a popup if the
  active layer isn't one. Only touches empty cells, so hand-painted tiles are
  left alone.
- **`Ctrl+Z`**: undo the last fill (single action, not a general undo stack —
  every other edit in Tilesmith is immediate and permanent).

---

## Elevation

`E` toggles elevation-paint mode. `[`/`]` step the brush value by 1 (Shift:
10), clamped to [0, 100]. Left click/drag paints elevation on the active
layer; right click/drag resets it to 0. The canvas tints elevated tiles
(cool blue → warm red as height increases) so the height map is visible while
you paint.

Elevation is per-tile (flat bands), not per-corner — there's no continuous
slope without a ramp (below). `max_step_height` (`GameConfig`, default 4,
read-only in Tilesmith — shown in the status bar) is how big an elevation
step an entity can walk between adjacent tiles with no ramp at all.

---

## Ramps (a.k.a. stairs)

`R` toggles ramp-paint mode — this is how you bridge two different
elevations so a character can actually walk between them, instead of being
walled off by `max_step_height`.

"Stairs" and "ramps" are the same mechanic here, not two different features —
whichever word you use depends only on what art you put on the tile. The
walkability graph and the height-lift interpolation (below) don't know or
care whether the tile *looks* like a smooth slope or a stepped staircase;
they always do the same continuous blend. So: pick stair-look or ramp-look
art in the palette same as any other tile, then mark that cell's bridging
axis in ramp mode — the mechanical behavior is identical either way. (A
chunky/retro *pop* instead of a smooth blend, for a deliberately blocky stair
look, isn't currently supported — it's always smooth.)

A ramp is a **single tile** that bridges its two opposite neighbors along one
axis. You don't enter a height delta by hand — it's inferred from whatever
elevation the two neighboring tiles already have.

1. Paint your two elevations first (a low plateau and a high plateau,
   adjacent to each other) using the elevation brush above.
2. Toggle ramp mode (`R`). `[`/`]` swap the axis between north-south and
   east-west — the status bar shows which one is selected (`[ramp axis:
   N-S]` or `[ramp axis: E-W]`).
3. Left-click the tile *between* the two elevations, matching whichever axis
   they're adjacent along. A green line appears across the tile showing its
   axis; right-click erases it.
4. A bigger elevation gap than one ramp tile can bridge? Chain consecutive
   ramp tiles up the slope — each one only needs to bridge its own immediate
   neighbor, so a multi-tile staircase falls out for free.

Toggle `W` (walkability overlay, below) to confirm the ramp actually
connected — the line across the ramp's own axis should *not* show as
disconnected, while the rest of the cliff edge around it still should.

Ramps are bidirectional (walkable both up and down) along their own axis
only — they don't help diagonally, and they don't affect the other axis at
all. A ramp only ever needs to be placed on the axis the two elevations are
actually adjacent along.

In-game, crossing a ramp tile lifts the entity's screen height smoothly
between the two neighbor elevations instead of popping — this only applies
in single-map mode (not chunked/streamed World mode) as of this writing.

---

## Walkability overlay

`W` toggles a live overlay: a magenta line is drawn on every disconnected
cardinal (N/S/E/W) edge — i.e. everywhere an entity currently can't walk
between two adjacent tiles, whether from an unramped elevation cliff or
(once doors/obstacles exist) something else. The graph is rebuilt fresh every
frame, so it updates live as you paint elevation or ramps. Diagonal edges
are never drawn — movement gating doesn't consume them today, so a diagonal
line would imply a gameplay effect that doesn't exist.

---

## Collisions

`C` toggles collision-edit mode. Left click/drag places a rectangle;
`T` (while in collision mode) switches to triangle placement, and
`[`/`]` cycle the triangle's corner cut (NW/NE/SE/SW). Right-click removes
whatever shape is under the cursor. Each placed shape auto-captures the
elevation painted at that cell, so collision only applies near the entity's
current floor (single-map mode).

---

## Portals

`P` toggles portal-edit mode. Left click/drag places a rectangular trigger
region; clicking an existing portal opens a popup to set its target map path
and spawn column/row in that map. `Delete`/`Backspace` removes the selected
portal. Portals are saved alongside the map, in `data/portals/{map_id}.json`,
not inside the tilemap JSON itself.

---

## Camera

Arrow keys pan; middle-mouse drag also pans. Once a map exceeds the canvas
viewport, scrollbars appear — drag them like any other scrollbar (painting
is suppressed while dragging one, so you can't accidentally paint through
it). Mouse wheel over the canvas does nothing; over the palette panel it
scrolls the tileset grid.

There's no interactive zoom in Tilesmith — `tile_scale` is computed once
from the tileset's frame size when the map loads (targeting ~64px rendered
cells) and stays fixed for the session. This is separate from the in-game
camera's live scroll-wheel zoom.

---

## Saving and validation

`Ctrl+S` runs `validate()` before writing. If it finds problems, a popup
lists them with a "Save Anyway" option — validation is advisory, not a hard
block. Checks include:

- Orphaned tile GIDs (no matching tileset)
- Duplicate layer names
- Collision rects/triangles extending outside the map bounds
- **Ramps whose axis-neighbor falls outside the map bounds** (e.g. a ramp
  painted against the map edge with nothing on one side to bridge to)

---

## JSON format reference

One file per map in `data/tilemaps/{id}.json`. Top-level fields:

| Field | Type | Description |
|---|---|---|
| `id` | string | Map identifier. |
| `width`, `height` | int | Map size in tiles. |
| `iso_diamond_w`, `iso_diamond_h` | int | Tile footprint (world-step) in pixels; optional, falls back to the tileset's frame size. |
| `tilesets` | array | `{"first_gid": N, "source": "path/to/tileset.json"}` per tileset used. |
| `layers` | array | See below. |
| `collisions`, `collision_triangles` | array | World-space collision geometry, each entry carrying its own captured `elevation`. |
| `schema_version` | int | Stamped by Tilesmith on every save. |

Each entry in `layers`:

| Field | Type | Description |
|---|---|---|
| `name` | string | Layer name. |
| `z_index` | int | Omitted (defaults to 0) for ground layers; present for anything drawn above entities. |
| `tiles` (dense) or `objects` (sparse) | array | Tile GIDs. Tilesmith picks whichever encoding is smaller. |
| `elevation` | array of comma-separated row strings | Per-tile elevation [0–100]; omitted entirely if the whole layer is flat. |
| `material_overrides` | array | `{"col","row","material"}`, sparse — per-cell terrain-material tag overriding the tileset default. |
| `ramps` | array | `{"col","row","axis":"ns"\|"ew"}`, sparse — see [Ramps](#ramps) above. |

---

## Quick reference

```
Camera:        Arrow keys pan  |  middle-drag pan  |  no interactive zoom

Paint:         Left click/drag paint  |  Right click/drag erase
               X / Y            flip horizontal / vertical
               F                fill empty cells on the ground layer
               Ctrl+Z           undo last fill

Modes:         E  elevation    [ / ]  brush value (Shift = ×10)
               R  ramps        [ / ]  axis (N-S / E-W)
               C  collisions   T  triangle mode   [ / ]  corner cut
               P  portals      Delete/Backspace  remove selected
               W  walkability overlay (read-only)

Layers:        Tab  cycle   1-9  jump to layer   +/-  add/delete   double-click  rename

Save:          Ctrl+S  validate + save (Save Anyway on warnings)

Quit:          Escape or Q
```
