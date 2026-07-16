# Writing Dialogue

Dialogues are defined as JSON files in the `data/dialogue/` directory (configurable via the `dialogue_dir` key in `game.json`). The engine loads them all at startup. This guide covers everything you need to write and wire up dialogue, from a simple NPC greeting to a branching conversation that starts quests and tracks state.

---

## How dialogue works

A dialogue is a **graph** of nodes connected by edges. The engine tracks a current node and advances through the graph based on player input. Progress and game state are stored in the flag store — the same shared store used by quests, so dialogue conditions and quest conditions work identically.

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

Top-level fields:

| Field | Type | Required | Description |
|---|---|---|---|
| `type` | string | no | `"graph"` (`"dialogue"` is accepted as an alias). Identifies the file to tools and the engine loader; may be omitted since the directory provides the context. |
| `id` | string | yes | Unique identifier for this dialogue graph. Snake_case. |
| `speaker` | string | no | Character name shown in the UI. Not used by core logic. |
| `variables` | object | no | Default flag values applied when dialogue starts (only if the flag is not already set). |
| `nodes` | array | yes | All nodes in the graph. |

---

## Node types

### Talk

Displays text. The player presses Select to advance, or Cancel to close the dialogue.

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
| `text` | string | yes | The line of dialogue shown to the player. |
| `next` | string | yes | Node to advance to. Use `"end"` to close the dialogue. |
| `metadata` | object | no | Arbitrary key-value pairs passed to the UI layer. See [Metadata](#metadata). |

---

### Choice

Presents a list of options. The player navigates with Move Up / Move Down and confirms with Select, or cancels with Cancel. Only visible choices are shown.

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

Each entry in `choices` is an edge:

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | yes | Text shown to the player for this option. |
| `target` | string | yes | Node to advance to when chosen. Use `"end"` to close. |
| `condition` | string | no | Flag expression; edge is hidden if false. See [Conditions](#conditions). |
| `actions` | array | no | Actions executed when this edge is taken. See [Actions](#actions). |
| `sequence` | string | no | Controls repetition. See [Sequencing](#sequencing). Defaults to `"none"`. |
| `min_visits` | integer | no | Minimum number of times this node must have been visited for this edge to appear. |

If all edges are hidden, the dialogue closes immediately.

---

### Event

Executes actions silently with no UI, then immediately advances to the next node. Useful for triggering sounds, starting quests, or mutating state mid-conversation.

```json
{
  "id": "n_pay",
  "type": "event",
  "actions": ["play_sound('coin')", "quest_start(\"find_sword\")"],
  "next": "n2"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `id` | string | yes | Unique within this graph. |
| `type` | string | yes | Must be `"event"`. |
| `actions` | array | yes | Actions to execute. Must not be empty. |
| `next` | string | yes | Node to advance to. Use `"end"` to close. |

Chains of event nodes are flushed in a single frame — no UI is shown between them.

---

### End

Closes the dialogue. You can also use `"end"` as the value of any `next` or `target` field without defining an explicit end node.

```json
{
  "id": "n_bye",
  "type": "end"
}
```

---

## Conditions

Conditions control whether a choice edge is visible. They are evaluated against the flag store.

```json
{ "condition": "gold >= 5" }
{ "condition": "!paid_innkeeper" }
{ "condition": "level >= 3 && sword_equipped" }
{ "condition": "quest_started(find_sword)" }
```

An absent or empty condition means the edge is always visible.

### Operators

| Category | Operators |
|---|---|
| Comparison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Boolean | `&&` (and), `\|\|` (or), `!` (not) |
| Grouping | `(` `)` |

### Values

- **Integers:** `5`, `-3`
- **Booleans:** `true`, `false` — equivalent to `1` and `0`; any non-zero value is truthy
- **Identifiers:** `gold`, `paid_innkeeper` — resolved from the flag store; missing flags default to `0`

### Quest condition helpers

Quest state can be checked with named helpers instead of raw flag values:

| Helper | Meaning |
|---|---|
| `quest_started(quest_id)` | Quest has been started |
| `quest_resolved(quest_id)` | Quest is over (any ending) |
| `quest_failed(quest_id)` | Quest ended in failure |
| `quest_at(quest_id, stage_name)` | Quest is at a specific named stage |

```json
{ "condition": "quest_at(find_sword, complete_helped)" }
{ "condition": "quest_failed(escort_merchant)" }
```

Prefer these over raw flag comparisons — they stay correct even if stage sequences are renumbered.

---

## Actions

Actions are listed as an array of strings. They run when an event node is reached or when a choice edge is taken.

```json
"actions": [
  "gold -= 5",
  "paid_innkeeper = true",
  "play_sound('coin')",
  "quest_start(\"find_sword\")"
]
```

### Flag mutations

Modify the flag store directly.

| Syntax | Meaning |
|---|---|
| `flag = value` | Set flag to value |
| `flag += value` | Add value to flag |
| `flag -= value` | Subtract value from flag |

Values can be integers (`5`, `-1`) or booleans (`true`, `false`).

### Engine hooks

Call engine functions such as audio, quests, or animations. Arguments use single quotes for strings.

```json
"actions": [
  "play_sound('coin')",
  "give_item('health_potion', 3)",
  "trigger_event('door_opens')"
]
```

### Quest actions

| Action | Effect |
|---|---|
| `quest_start("quest_id")` | Starts the quest (sets flag to first stage). No-op if already started. |
| `quest_advance("quest_id", "stage_name")` | Advances quest to the named stage. |

---

## Sequencing

The `sequence` field on a choice edge controls how often it appears.

| Value | Behaviour |
|---|---|
| `"none"` | Always visible (default) |
| `"once"` | Visible once; hidden permanently after taken |
| `"cycle"` | Rotates with other `cycle` edges on this node, one per visit |
| `"random"` | One random `random` edge shown per visit |

`"once"` is the most common — use it for choices that should only be available one time, like paying for a room or receiving a quest reward.

Non-sequenced edges are always shown alongside whichever cycled or random edge is active.

---

## Metadata

Any node can carry a `metadata` object — arbitrary string key-value pairs passed to the UI layer. The dialogue system ignores them; your presentation code interprets them.

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

Use metadata for character expressions, portrait cues, camera direction, ambient sounds, or anything else the UI needs.

---

## Graph-level variables

The `variables` field sets default flag values when the dialogue starts. Values are only written if the flag is not already set — they will not overwrite state from a previous session.

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

Use this to initialise flags that the graph's own conditions and actions depend on.

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
          "label": "I need a room for the night.",
          "target": "n_already_paid",
          "condition": "paid_innkeeper == true"
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
      "id": "n_already_paid",
      "type": "talk",
      "text": "You've already paid. Your room is ready.",
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

**How it flows:**

1. Player opens dialogue → `n0` (Talk) is shown.
2. Player presses Select → `n1` (Choice) appears.
3. First visit, gold ≥ 5, not yet paid: first and third choices are visible. Player picks "I need a room" → `gold -= 5`, `paid_innkeeper = true` execute, edge is marked once-used → `n_pay` (Event) fires `play_sound('coin')` → `n2` (Talk) shown → dialogue ends.
4. Second visit: first choice hidden (once taken), second choice now visible (paid_innkeeper is true).

---

## Naming conventions

| Thing | Convention | Example |
|---|---|---|
| Graph `id` | `snake_case` | `innkeeper_intro`, `merchant_haggle` |
| Node `id` | `snake_case` | `n0`, `n_pay`, `n_already_paid` |
| Flag names | `snake_case` | `paid_innkeeper`, `sword_obtained` |
| Dialogue file | `{graph_id}.json` | `innkeeper_intro.json` |

---

## Validation rules

The loader rejects graphs that violate any of these rules and prints a warning to stderr. The rest of the graphs still load.

- `type`, if present, should be `"graph"` (or the alias `"dialogue"`) — anything else prints a warning but the file still loads
- `id` must be non-empty
- All node `id` values must be unique within the graph
- All `next` and `target` values must point to an existing node or `"end"`
- Talk nodes must have non-empty `text`
- Choice nodes must have at least one choice with non-empty `label` and `target`
- Event nodes must have at least one action
- Node `type` must be one of: `talk`, `choice`, `event`, `end`
- Choice `sequence` must be one of: `none`, `once`, `cycle`, `random`

---

## Quick reference

```
Node types:     talk  choice  event  end

Condition ops:  ==  !=  <  >  <=  >=  &&  ||  !  ( )

Flag actions:   flag = value  |  flag += value  |  flag -= value

Quest actions:  quest_start("quest_id")
                quest_advance("quest_id", "stage_name")

Quest checks:   quest_started(quest_id)
                quest_resolved(quest_id)
                quest_failed(quest_id)
                quest_at(quest_id, stage_name)

Sequencing:     none  once  cycle  random

Special target: "end"  (closes dialogue from any next or target field)
```
