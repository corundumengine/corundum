# Writing Dialogue

Dialogue is written as JSON files in `data/dialogue/` (the `dialogue_dir` key in `game.json` overrides that). The engine loads them all at startup.

---

## How dialogue works

A dialogue is a graph of nodes joined by edges. The engine keeps track of a current node and moves forward as the player acts. Progress lives in the flag store, the same one quests use, so dialogue conditions and quest conditions behave the same way.

---

## A minimal dialogue

```json
{
  "type": "graph",
  "id": "innkeeper_greet",
  "speaker": "Innkeeper",
  "nodes": [
    {
      "id": "n0",
      "type": "talk",
      "text": "Welcome, traveller.",
      "next": "end"
    }
  ]
}
```

The fields at the top level:

| Field | Type | Required | Description |
|---|---|---|---|
| `type` | string | no | `"graph"` (`"dialogue"` is accepted as an alias). Identifies the file to tools and the loader; you can leave it out, since the directory already says what the file is. |
| `id` | string | yes | Unique identifier for this graph. Snake_case. |
| `speaker` | string | no | Character name shown in the UI. Core logic ignores it. |
| `actor_id` | string | no | Optional stable NPC id this graph belongs to. Lets quest and divert logic match a graph to a specific NPC (see [Stable NPC identity](#stable-npc-identity)). |
| `variables` | object | no | Default flag values applied when the dialogue starts (only if the flag isn't already set). |
| `nodes` | array | yes | All nodes in the graph. |

---

## Node types

### Talk

Shows a line of text. The player presses Select to advance or Cancel to close the dialogue.

```json
{
  "id": "n0",
  "type": "talk",
  "text": "What brings you here?",
  "next": "n1"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Unique within this graph. |
| `type` | string | yes | Must be `"talk"`. |
| `text` | string | yes | The line shown to the player. |
| `next` | string | yes | Node to move to next. Use `"end"` to close. |
| `once` | boolean | no | If true, the line is shown once; on a later visit it's skipped straight to `next`. |
| `metadata` | object | no | Arbitrary string pairs passed to the UI layer. See [Metadata](#metadata). |

---

### Choice

Shows a list of options. The player moves with Move Up / Move Down, confirms with Select, and cancels with Cancel. Only visible choices are shown.

```json
{
  "id": "n1",
  "type": "choice",
  "choices": [
    {
      "label": "I need a room for the night.",
      "target": "n_room",
      "condition": "gold >= 5",
      "actions": ["gold -= 5", "paid_innkeeper = true"],
      "sequence": "once"
    },
    {
      "label": "Just passing through.",
      "target": "n_bye"
    }
  ]
}
```

Each entry in `choices` is one edge:

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | yes | Text shown to the player. |
| `target` | string | yes | Node to go to when chosen. Use `"end"` to close. |
| `condition` | string | no | Flag expression; the edge is hidden when it's false. See [Conditions](#conditions). |
| `actions` | array | no | Actions run when the edge is taken. See [Actions](#actions). |
| `sequence` | string | no | How often the edge reappears. See [Sequencing](#sequencing). Defaults to `"none"`. |
| `min_visits` | integer | no | Minimum number of times this node must have been visited before the edge shows. |

If every edge is hidden, the dialogue closes on the spot.

---

### Event

Runs actions silently, with no UI, then moves straight to the next node. Handy for sounds, quest starts, or fiddling with state mid-conversation.

```json
{
  "id": "n_pay",
  "type": "event",
  "actions": ["play_sound('coin')", "quest_start('find_sword')"],
  "next": "n2"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Unique within this graph. |
| `type` | string | yes | Must be `"event"`. |
| `actions` | array | yes | Actions to run. Must not be empty. |
| `next` | string | yes | Node to move to next. Use `"end"` to close. |

A chain of event nodes resolves in a single frame, with nothing shown in between.

---

### End

Closes the dialogue. You can also just write `"end"` as the value of any `next` or `target` field instead of defining an end node.

```json
{
  "id": "n_bye",
  "type": "end"
}
```

---

## Conditions

A condition decides whether a choice edge is visible. Conditions are compiled once at load time and validated right away.

```json
{ "condition": "gold >= 5" }
{ "condition": "!paid_innkeeper" }
{ "condition": "level >= 3 && sword_equipped" }
{ "condition": "quest_is_started(find_sword)" }
```

Leave the condition out and the edge is always visible.

### Compilation and validation

Each `condition` string is parsed once and compiled into a reusable expression. If it doesn't parse, the whole graph fails to load with an error that names the offending choice. There's no silent failure here: a typo keeps the dialogue from ever loading.

Once all graphs, quests, and items are loaded, the engine also cross-checks references and prints a warning to stderr for anything that doesn't resolve: an unknown quest id in `quest_is_started` / `quest_is_resolved` / `quest_is_failed`, an unknown quest or stage in `quest_is_at`, an unknown item in `give_item` / `take_item`, or an unknown graph or node in `goto_graph`. Warnings don't stop the graph from loading. The bad reference just evaluates to `false` (a hidden choice) or gets dropped at runtime.

Flag keys in `actions` aren't validated at all. Only keys that appear in a `condition` are checked. Actions apply whatever they say. Flags are free-form, so there's no registry to check against.

### Operators

| Category | Operators |
|---|---|
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Boolean | `&&` (and), `\|\|` (or), `!` (not) |
| Grouping | `(` `)` |

### Values

- **Integers:** `5`, `-3`
- **Booleans:** `true`, `false`, which are just `1` and `0`; anything non-zero is truthy
- **Identifiers:** `gold`, `paid_innkeeper`, looked up in the flag store; a missing flag reads as `0`

A bare identifier with no comparison, like `{ "condition": "sword_equipped" }`, is a truthiness check: the edge shows when the value is non-zero.

Identifiers may contain dots, so namespaced keys parse as one token: `quest.find_sword >= 2`, `local.chest_looted == 1`, `rep.faction == 1`.

When the right-hand side of a comparison is a boolean literal, `==` and `!=` compare by truthiness rather than exact value. `paid_innkeeper == true` is true for any non-zero value.

### Quest condition helpers

Rather than reading raw flag values, you can check quest state with named helpers:

| Helper | Meaning |
|---|---|
| `quest_is_started(quest_id)` | The `quest.<id>` flag is set, meaning any stage reached, including a completed one |
| `quest_is_resolved(quest_id)` | The quest is on a resolved stage (completed or failed) |
| `quest_is_failed(quest_id)` | The quest ended in failure |
| `quest_is_at(quest_id, stage_name)` | The quest is at a specific named stage |

```json
{ "condition": "quest_is_at(find_sword, complete_helped)" }
{ "condition": "quest_is_failed(escort_merchant)" }
```

[Writing Quests](writing-quests.md) describes the stage model these helpers read from.

### Item and reputation helpers

| Helper | Meaning |
|---|---|
| `has_item(item_id)` | The `item.<id>` flag is non-zero |
| `item_count(item_id)` | The `item.<id>` count |
| `rep(faction_id)` | The `rep.<id>` count |

```json
{ "condition": "has_item(health_potion)" }
{ "condition": "item_count(arrows) >= 3" }
{ "condition": "rep(merchants_guild) >= 2" }
```

### Node-visit helpers

`seen(node_id)` and `visits(node_id)` read how many times a node in the *same graph* has been entered, using the engine's internal `_visit_<graph>_<node>` counter. They're the author-facing way to gate a line on whether another node was already shown:

| Helper | Meaning |
|---|---|
| `seen(node_id)` | The named node has been entered at least once (truthy) |
| `visits(node_id)` | The number of times the named node has been entered |

```json
{ "condition": "seen(reveal_clue)" }
{ "condition": "visits(blacksmith_greeting) >= 2" }
```

They resolve against the graph that owns the condition, so `seen('n0')` and a `seen('n0')` in a different graph read different counters. Outside a dialogue graph (for example in a quest objective's `done_condition`) the helpers have no graph to resolve against and evaluate to false/0.

---

## Local vs. global state

Every flag lives in one shared store, and by default a flag is global and permanent. Once set, it stays set everywhere, forever:

```json
{ "actions": ["cave_explored = true"] }
```

When the same key would otherwise collide across places (a "chest looted" in every dungeon, a "met the elder" in every town), prefix it with `local.`. A `local.<key>` reference is rewritten to `zone.<zone_id>.<key>` against the current zone, which is the tilemap's file stem for interiors or the world manifest's directory name in overworld mode:

```json
{ "actions": ["local.chest_looted = true"] }
{ "condition": "local.chest_looted == 1" }
```

In the `cave` zone that writes/reads `zone.cave.chest_looted`; in the `village` zone the same expression hits a different, empty key. This is just sugar over a namespaced key. There's no second container, and the state serializes with everything else.

A few things worth remembering:

- `local.<key>` needs an active zone. With no zone context, the reference is left alone and behaves like a bare global key.
- To wipe one zone's state, erase its whole `zone.<id>.` key range (`corundum::world::reset_zone(flags, "cave")` from game code).
- Bare keys are global and permanent. Use them deliberately: reputation, quest stages, player-level facts.

---

## Actions

Actions are an array of strings. They run when an event node is reached or a choice edge is taken.

```json
"actions": [
  "gold -= 5",
  "paid_innkeeper = true",
  "play_sound('coin')",
  "quest_start('find_sword')"
]
```

Each action string is parsed once at load time; a malformed one is a hard load error.

### Flag mutations

These modify the flag store directly. `local.<key>` targets resolve to the zone-scoped key, same as in conditions.

| Syntax | Meaning |
|---|---|
| `flag = value` | Set flag to value |
| `flag += value` | Add value to flag |
| `flag -= value` | Subtract value from flag |

Values are integers (`5`, `-1`) or booleans (`true`, `false`).

### Engine hooks

These call engine functions. String arguments use single quotes.

| Action | Effect |
|---|---|
| `play_sound('name')` | Play the named sound |
| `quest_start('quest_id')` | Start the quest (sets the flag to the first stage). No-op if already started. |
| `quest_advance('quest_id', 'stage_name')` | Advance the quest to the named stage |
| `give_item('item_id'[, count])` | Add `count` (default 1) to the `item.<id>` flag |
| `take_item('item_id'[, count])` | Subtract `count` (default 1) from the `item.<id>` flag; the flag is removed at 0 |
| `reputation('faction_id', amount)` | Add `amount` to the `rep.<id>` flag |

```json
"actions": [
  "play_sound('coin')",
  "give_item('health_potion', 3)",
  "reputation('merchants_guild', 1)"
]
```

Any hook name the engine doesn't recognise goes to the game's `on_event` callback, which can handle it or ignore it (an ignored hook logs a warning). That's how you wire up custom events like opening a door or spawning an NPC.

### Divert actions

| Action | Effect |
|---|---|
| `goto_graph('graph_id', 'node_id')` | Switch to the named graph at the named node, pushing the current graph and resume point onto a call stack. |
| `return_graph()` | Pop the stack and resume where the current graph would have gone. Ends the dialogue if the stack is empty. |

See [Cross-graph divert](#cross-graph-divert-goto_graph-return_graph).

---

## Sequencing

The `sequence` field on a choice edge controls how often that edge reappears.

| Value | Behaviour |
|---|---|
| `"none"` | Always visible (default) |
| `"once"` | Visible once, then hidden for good |
| `"cycle"` | Rotates with the other `cycle` edges on this node, one per visit |
| `"random"` | One of the `random` edges is shown per visit |

`"once"` is the workhorse. Use it for anything that should only happen one time, like paying for a room or claiming a quest reward.

Non-sequenced edges always show alongside whichever cycled or random edge is active. The cycle slot and random pick come from the node's visit count (plus, for random, a deterministic hash of the graph id, node id, and visit count), so nothing extra needs to be stored.

---

## Metadata

Any node can carry a `metadata` object: arbitrary string pairs handed to the UI layer. The dialogue system ignores them; your presentation code interprets them.

```json
{
  "id": "n0",
  "type": "talk",
  "text": "Welcome, traveller.",
  "next": "n1",
  "metadata": {
    "emotion": "friendly",
    "portrait": "innkeeper_smile",
    "camera": "close_up"
  }
}
```

Use metadata for expressions, portrait cues, camera direction, ambient sounds, whatever the UI needs.

---

## Cross-graph divert: `goto_graph` / `return_graph`

A dialogue can jump into another graph and come back. `goto_graph('graph_id', 'node_id')` switches to the named graph at the named node, pushing the current graph and resume point onto a small call stack. `return_graph()` pops that stack and resumes where the current graph would have gone, or ends the conversation if the stack is empty.

```json
{
  "id": "n_shop",
  "type": "event",
  "actions": ["goto_graph('shopkeeper_intro', 'n0')"],
  "next": "n_after_shop"
}
```

`n_after_shop` is the resume point. After the shopkeeper's graph calls `return_graph()`, the conversation continues at `n_after_shop`, not back at the diverting event node, which would just re-fire the divert.

A common hub-and-spoke setup:

- **Hub graph** `elder_maren_hub`: a greeting plus a menu of topics, where each topic's event node does `goto_graph('elder_maren_<topic>', 'n0')`.
- **Spoke graphs:** one per topic, each ending with an event node that calls `return_graph()`.

A spoke can divert to another spoke too; the stack unwinds in reverse. `return_graph()` on an empty stack ends the dialogue.

Diverts are checked at load time against the full graph registry. A `goto_graph` that names a missing graph or node is reported at startup.

---

## Graph-level variables

The `variables` field sets default flag values when the dialogue starts. Values are only written if the flag isn't already set, so they won't clobber state from an earlier session.

```json
{
  "id": "merchant_intro",
  "variables": {
    "merchant_visits": 0,
    "haggle_used": false
  },
  "nodes": [ ... ]
}
```

Values are integers or booleans. Use this to seed flags the graph's own conditions and actions depend on.

---

## Stable NPC identity

An NPC's internal entity handle changes whenever its map chunk reloads, so quests and saves can't hold onto it. Instead, NPCs carry a stable authoring id (the `id` from their spawn-point entry) that survives chunk respawns.

A graph can be tied to its NPC with the top-level `actor_id` field, matching that authored id. From game code, `corundum::entities::find_actor(world, id)` turns the id back into a live entity handle for quest and save logic.

Per-NPC runtime state lives in flags under `npc.<id>.<key>`:

```json
{ "actions": ["npc.brann.alive = false"] }
{ "condition": "npc.brann.alive == 1" }
```

These keys stay in the flag store (and the save file) whether or not the NPC's map chunk is currently loaded.

---

## A complete example

```json
{
  "type": "graph",
  "id": "innkeeper_intro",
  "speaker": "Innkeeper",
  "nodes": [
    {
      "id": "n0",
      "type": "talk",
      "text": "Welcome, traveller. What brings you here?",
      "next": "n1",
      "metadata": { "emotion": "friendly" }
    },
    {
      "id": "n1",
      "type": "choice",
      "choices": [
        {
          "label": "I need a room for the night.",
          "target": "n_pay",
          "condition": "gold >= 5 && !paid_innkeeper",
          "actions": ["gold -= 5", "paid_innkeeper = true"],
          "sequence": "once"
        },
        {
          "label": "Just passing through.",
          "target": "n_bye"
        }
      ]
    },
    {
      "id": "n_pay",
      "type": "event",
      "actions": ["play_sound('coin')"],
      "next": "n2"
    },
    {
      "id": "n2",
      "type": "talk",
      "text": "That'll be 5 gold. Right this way.",
      "next": "end"
    },
    {
      "id": "n_bye",
      "type": "talk",
      "text": "Safe travels. Watch the road south.",
      "next": "end"
    }
  ]
}
```

Stepping through it:

1. The player opens the dialogue and sees `n0`.
2. Pressing Select brings up the choice node `n1`.
3. On the first visit, with gold ≥ 5 and the room not yet paid for, the player sees both options. Choosing "I need a room" runs `gold -= 5` and `paid_innkeeper = true`, marks the edge as used, then fires `play_sound('coin')` at `n_pay` and shows `n2` before the dialogue ends.
4. On a second visit, the first choice is gone (taken once) and the second is still there.

---

## Naming conventions

| Thing | Convention | Example |
|---|---|---|
| Graph `id` | `snake_case` | `innkeeper_intro`, `merchant_haggle` |
| Node `id` | `snake_case` | `n0`, `n_pay`, `n_bye` |
| Flag names | `snake_case` | `paid_innkeeper`, `sword_obtained` |
| Quest helpers | `quest_is_<state>` | `quest_is_started`, `quest_is_at` |
| Item helpers | `has_item` / `item_count` | `has_item(health_potion)` |
| Dialogue file | `{graph_id}.json` | `innkeeper_intro.json` |

---

## Validation rules

The loader rejects a graph that breaks any of these and prints a warning to stderr; the remaining graphs still load.

- `type`, if present, should be `"graph"` (or `"dialogue"`); anything else warns but the file still loads
- `id` must be non-empty
- Node `id`s must be unique within the graph
- Every `next` and `target` must point at an existing node or `"end"`
- Talk nodes need non-empty `text`
- Choice nodes need at least one choice with non-empty `label` and `target`
- Event nodes need at least one action, and every action must parse
- Node `type` must be `talk`, `choice`, `event`, or `end`
- Choice `sequence` must be `none`, `once`, `cycle`, or `random`
- Event nodes can't form a cycle (an event chain that loops back on itself is rejected)
- Conditions must compile (a syntax error rejects the graph); unresolved quest/item/graph references in conditions and actions print a startup warning

---

## Quick reference

```
Node types:     talk  choice  event  end

Condition ops:  ==  !=  <  >  <=  >=  &&  ||  !  ( )

Flag actions:   flag = value  |  flag += value  |  flag -= value

Engine hooks:   play_sound('name')
                quest_start('quest_id')
                quest_advance('quest_id', 'stage_name')
                give_item('item_id'[, count])
                take_item('item_id'[, count])
                reputation('faction_id', amount)

Divert actions: goto_graph('graph_id', 'node_id')
                return_graph()

Quest helpers:
  quest_is_started(quest_id)    - quest flag is set (any stage)
  quest_is_resolved(quest_id)   - quest is over (completed or failed)
  quest_is_failed(quest_id)     - quest ended in failure
  quest_is_at(quest_id, stage)  - quest is at a specific named stage

Item / rep helpers:
  has_item(item_id)    - item.<id> is non-zero
  item_count(item_id)  - item.<id> count
  rep(faction_id)      - rep.<id> count

State scoping:
  local.<key>   ->   zone.<zone_id>.<key>  (per-zone, e.g. local.chest_looted -> zone.cave.chest_looted)
  bare key      ->   global, persistent
  npc.<id>.<key> ->  per-NPC state, survives chunk reloads

Sequencing:     none  once  cycle  random

Special target: "end"  (closes dialogue from any next or target field)
```
