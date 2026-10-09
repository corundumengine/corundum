# Equipment

Corundum's equipment is a flag convention plus helper functions: items and slots are authored
data, and the gameplay framework moves the flags the Inventory panel displays (`equip_item()`,
`unequip_item()`, `toggle_equip()`). Real RPG rules — stats, requirements, combat effects — stay
above the framework, in the game (Keystone) or a later `rules` layer.

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

The Inventory hub tab toggles the highlighted item's equipped state on Activate (Enter / gamepad
A). Equipping an item clears whichever item previously occupied its slot, so a slot holds at most
one item. Items that are not held, unknown, or have no slot report a message and change nothing.

The same helpers are available to game code (dialogue events, the game's own screens, an input
system) as `corundum::gameplay::item::equip_item()`, `unequip_item()` and `toggle_equip()`. The
Inventory re-reads the flags when it rebuilds its lines (on tab open or switch, or after an
equip). If an item leaves the inventory while equipped, its `equip.` flag is cleared the next time
another item is equipped in that slot; nothing clears it merely because the item left, so game
code that drops an equipped item should clear the flag itself.
