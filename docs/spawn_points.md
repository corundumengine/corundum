# Spawn Points

Spawn points define where actors (NPCs) appear on a map at load time. They live in `data/spawn_points/`, one file per tilemap, named after the tilemap (e.g., `world.json` for `game/data/tilemaps/world.json`).

Map geometry and actor data are kept separate by design — you can change who appears on a map without touching the tilemap file.

The player is not defined here. Player placement is handled in code and will eventually depend on which entrance the player used to enter the map.

---

## File format

A spawn points file is a JSON object with a single `"actors"` array:

```json
{
  "actors": [
    {
      "col": 13,
      "row": 4,
      "sprite": "innkeeper",
      "dialogue": "innkeeper_intro"
    },
    {
      "col": 28,
      "row": 16,
      "sprite": "villager",
      "dialogue": "villager_generic"
    },
    { "col": 5, "row": 10, "sprite": "guard" }
  ]
}
```

The file is optional. If no file exists for the current map, no actors are spawned.

---

## Adding actors to a map

1. Create `data/spawn_points/<map_name>.json` (where `<map_name>` matches the tilemap filename stem, e.g. `world`).
2. Add entries to the `"actors"` array.
3. Run the game — actors spawn at the given tile positions.

No changes to the tilemap JSON or any source file are needed.

---

## Reference

### Actor fields

| Field      | Required | Type   | Description                                                        |
| ---------- | -------- | ------ | ------------------------------------------------------------------ |
| `col`      | ✓        | int    | Zero-based tile column. Must be `>= 0`.                            |
| `row`      | ✓        | int    | Zero-based tile row. Must be `>= 0`.                               |
| `sprite`   | ✓        | string | Sprite name. Must be non-empty and registered in the sprite index. |
| `dialogue` |          | string | Dialogue graph ID. Omit for non-interactive actors.                |

### Validation

`load_actors` throws `ActorLoadError` on any schema or parse failure:

- File exists but is not valid JSON
- Root is not an object
- `"actors"` key is missing or not an array
- Any entry is not an object
- `col` or `row` is missing, not an integer, or negative
- `sprite` is missing, not a string, or empty
- `dialogue` is present but not a string

A missing file is not an error — it results in an empty actor list.
