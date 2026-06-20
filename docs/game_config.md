# Game Configuration

The engine reads `game/data/game.json` at startup before any other asset is loaded. Every field is optional — absent keys use the values shown below as defaults. This means a minimal `{}` is a valid config file, and adding new fields in future versions will not break existing files.

If `game/data/game.json` cannot be opened, or any field fails validation, the engine exits immediately with a `[crpg] FATAL:` message.

---

## Reference

### Window

| Key         | Type  | Default | Description                             |
| ----------- | ----- | ------- | --------------------------------------- |
| `win_w`     | float | `800.0` | Window width in pixels. Must be `> 0`.  |
| `win_h`     | float | `600.0` | Window height in pixels. Must be `> 0`. |
| `framerate` | uint  | `60`    | GLFW framerate limit. Must be `> 0`.    |

### Rendering

| Key            | Type  | Default | Description                                                |
| -------------- | ----- | ------- | ---------------------------------------------------------- |
| `sprite_scale` | uint  | `2`     | Scale factor applied to all entity sprites. Must be `> 0`. |
| `tile_scale`   | uint  | `2`     | Scale factor applied to all tilemap tiles. Must be `> 0`.  |

`sprite_scale` and `tile_scale` are usually the same value. Separating them allows tiles and sprites to be scaled independently (e.g. high-resolution sprite sheets on a pixel-art tileset).

The renderer uses nearest-neighbour filtering, so integer scales (1, 2, 3, …) produce perfectly uniform pixels. The config enforces unsigned integer values for both fields.

### Gameplay

| Key               | Type  | Default | Description                                                                        |
| ----------------- | ----- | ------- | ---------------------------------------------------------------------------------- |
| `interact_radius` | float | `80.0`  | Distance (world pixels) within which the player can start dialogue. Must be `> 0`. |
| `player_speed`    | float | `200.0` | Player movement speed (pixels per second). Must be `> 0`.                          |

### Asset paths

All paths are relative to the working directory (the project root when running from `build/`).

| Key            | Type   | Default                           | Description                                  |
| -------------- | ------ | --------------------------------- | -------------------------------------------- |
| `font_dir`     | string | `"game/data/fonts"`               | Directory that contains font files.          |
| `game_font`    | string | `"CrimsonText-Regular.ttf"`       | Font file name inside `font_dir`.            |
| `tilemap_path` | string | `"game/data/tilemaps/world.json"` | Path to the tilemap JSON file to load.       |
| `sprites_dir`  | string | `"game/data/sprite_sheets"`       | Path to the sprite sheets.                   |
| `dialogue_dir` | string | `"game/data/dialogue"`            | Directory that contains dialogue JSON files. |

Non-empty validation applies to all string fields — an explicit `""` throws at startup.

### Dialogue rendering (`dialogue_render`)

Nested object. All sub-keys are optional; absent keys use their defaults.

| Key                 | Type  | Default | Description                                                                    |
| ------------------- | ----- | ------- | ------------------------------------------------------------------------------ |
| `font_size_speaker` | uint  | `26`    | Font size for the speaker name label.                                          |
| `font_size_body`    | uint  | `22`    | Font size for body text and choice labels.                                     |
| `font_size_prompt`  | uint  | `18`    | Font size for the `[Select] Continue` / `[Cancel] Close` prompt.               |
| `margin`            | float | `20.0`  | Inset (pixels) from the panel edge to text.                                    |
| `line_spacing`      | float | `32.0`  | Vertical gap between lines of text (pixels).                                   |
| `panel_height_frac` | float | `0.32`  | Dialogue panel height as a fraction of the window height. Must be in `(0, 1)`. |

---

## Example

```json
{
  "win_w": 1280.0,
  "win_h": 720.0,
  "framerate": 60,
  "interact_radius": 80.0,
  "player_speed": 200.0,
  "sprite_scale": 2,
  "tile_scale": 2,
  "font_dir": "game/data/fonts",
  "game_font": "CrimsonText-Regular.ttf",
  "tilemap_path": "game/data/tilemaps/world.json",
  "sprites_dir": "game/data/sprite_sheets",
  "dialogue_dir": "game/data/dialogue",
  "dialogue_render": {
    "font_size_speaker": 26,
    "font_size_body": 22,
    "font_size_prompt": 18,
    "margin": 20.0,
    "line_spacing": 32.0,
    "panel_height_frac": 0.32
  }
}
```

---

## Architecture notes

`GameConfig` and `DialogueRenderConfig` live in `core/game/game_config.hpp` with no GLFW dependency — they are plain data structs. `load_game_config(path)` throws `GameConfigLoadError` on any failure. The config is loaded at the very top of `main()` before any other asset; a failed load exits immediately.

The scale constants `sprite_scale` and `tile_scale` are passed as explicit parameters to `render()` and `render_tilemap()` respectively — they are not compile-time constants. `dialogue_render` is passed as `const DialogueRenderConfig&` to `render_dialogue()` so the renderer takes only the slice it needs.
