# Character animation

A character is a set of sheets that share a name up to the last `_`. Point an actor's `sprite`, or
`player.idle_sprite` in `game.json`, at the idle sheet, and the engine finds the rest:

| Sprite | Plays | Rate |
| --- | --- | --- |
| `skeleton_idle` | Standing | The sheet's `fps` |
| `skeleton_walk` | Moving | The sheet's `fps`, scaled by speed |
| `skeleton_attack` | A wind-up at an enemy | Fitted to the wind-up: the middle frame meets the landing |
| `skeleton_hurt` | A fall in Health | The sheet's `fps` |
| `skeleton_die` | Falling | The sheet's `fps` |

Only the idle sheet is required. A character missing one of the others doesn't play that motion: with
no die sheet, a fallen enemy disappears at once. An attack, hurt or die sheet needs frames for all
eight directions, or a `default` clip; one that leaves a direction empty counts as missing. The
player's walk sheet comes from `player.walk_sprite`, not from this naming.

Give each sheet the same frame size and `walk_around_offset`, or the character shifts when its sheet
changes. Draw the strike around the middle of the attack sheet. Attack and death art often spreads
well outside the idle pose, so leave the whole frame in the sheet.

A fallen enemy leaves the fight at once and disappears when its death clip ends. The protagonist's
death clip plays before `encounter_lost` is raised, and the protagonist stays on the last frame.
