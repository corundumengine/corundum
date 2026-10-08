# Equipment

Corundum has no equipment system. Items and slots are authored data, and equipping is a flag
convention the game owns; the gameplay framework only displays what the flags say. That keeps
stats, requirements and combat — real RPG rules — above the framework, in the game (Keystone) or
a later `rules` layer.

## The convention

- The player holds an item through the inventory flag `item.<item id>` (a count, as for any item).
- The flag `equip.<slot>.<item id>` = `1` marks that item as equipped in that slot. Clear the
  flag (set it to `0`) to unequip.

The framework recognizes no fixed slot names. A slot is whatever string an item's
`apparel.slot` field holds, so the game's apparel JSON defines the slot vocabulary. The Inventory
hub tab's left column lists the distinct slots found among held apparel items, in sorted order,
showing the equipped item's name or `(empty)`.

Use one spelling per slot, in both the item JSON and the `equip.` flags — `head` and `Head` are
different slots to the display.

## Example

```json
{
  "id": "travel_cloak",
  "name": "Travel Cloak",
  "apparel": { "slot": "body" }
}
```

With `item.travel_cloak = 1` and `equip.body.travel_cloak = 1`, the Inventory's `body` row shows
"Travel Cloak". Without the `equip.` flag it shows `(empty)`.

## Slot names

`docs/equipment.md` gives the usual names as examples only; nothing in the engine enforces them:

| Slot | Holds |
|---|---|
| `head` | Helmets, hats, hoods |
| `body` | Armour, robes, cloaks |
| `hands` | Gloves, gauntlets |
| `feet` | Boots, greaves |
| `weapon` | Weapons, shown the same way |

## Who equips

The framework offers no equip or unequip action and no hook. The game sets and clears the
`equip.<slot>.<item id>` flags — from dialogue events, its own screens, or an input handling
system — and the Inventory re-reads them when it rebuilds its lines (on tab open or switch). If
an item leaves the inventory, clear its `equip.` flag as well; nothing does that automatically.
