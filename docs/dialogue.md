# Dialogue System

The dialogue system drives NPC conversations through a directed graph of nodes declared entirely in JSON. No dialogue logic lives in C++ — the engine reads the graph, evaluates conditions against a shared variable store, executes actions, and advances state in response to player input. Adding or changing a conversation means editing a JSON file, not touching source code.

---

## Concepts

### Nodes

Each conversation is a graph of **nodes**. There are four kinds:

| Type     | Purpose                                                                                                       |
| -------- | ------------------------------------------------------------------------------------------------------------- |
| `talk`   | The NPC speaks. Advances to `"next"` on player input.                                                         |
| `choice` | The player picks from a list of labelled options.                                                             |
| `event`  | Executes actions silently — no UI, no input required — then advances to `"next"` automatically.               |
| `end`    | Terminates the conversation. Never appears explicitly — use the reserved target `"end"` on any outgoing edge. |

Every graph must declare an `"id"` field. The first node in the `"nodes"` array is the entry point.

### The Flag Store

A single **flag store** is shared across all conversations for the entire session. Variables are named integers. The flag helpers treat any value ≥ 1 as "set" (truthy), and 0 as "unset" (falsy). This means boolean flags and numeric variables live in the same store — `gold`, `reputation`, and `paid_innkeeper` are all just integers.

Variables survive across conversations. If the player pays the innkeeper and `paid_innkeeper` is set to `1`, that value persists for the whole session.

Internal variables managed by the engine are prefixed with `_` — do not use that prefix in authored names.

### Graph Variables

A graph can declare default variable values in a top-level `"variables"` object. These are copied into the flag store when the conversation starts, **without overwriting** values already set from prior sessions. Use this to seed initial state for a conversation that doesn't rely on global setup.

```json
{
  "id": "innkeeper_intro",
  "variables": {
    "gold": 10
  },
  "nodes": [...]
}
```

### Conditions

Choice edges support a `"condition"` field — a boolean expression evaluated against the flag store. An edge is hidden when the condition evaluates to false. No condition means the edge is always visible.

**Supported syntax:**

| Form               | Examples                                             |
| ------------------ | ---------------------------------------------------- |
| Integer literal    | `5`, `-3`                                            |
| Boolean literal    | `true`, `false`                                      |
| Variable           | `gold`, `paid_innkeeper`                             |
| Comparison         | `gold >= 5`, `reputation != 0`, `level == 3`         |
| Boolean operators  | `gold >= 5 && !paid_innkeeper`, `tired \|\| hungry`  |
| Parentheses        | `(gold >= 5) && !(banned == true)`                   |

A bare variable name is truthy when its value is ≥ 1: `!paid_innkeeper` is true when the flag is unset (value 0).

Comparing with `true` or `false` uses truthiness, not an exact integer match: `paid_innkeeper == true` means "paid_innkeeper is non-zero", regardless of how many times the flag was incremented.

### Actions

Both choice edges and `event` nodes can carry an `"actions"` array. Actions are executed when the edge is taken or the event node is reached.

**State mutations** are applied immediately to the flag store:

```
var = value       assign   (value may be an integer, true, or false)
var += value      add
var -= value      subtract
```

**Engine hook calls** are passed to the platform for dispatch — the dialogue system itself never executes them:

```
play_sound('coin')
start_quest('inn_stay')
trigger_event('open_door', 'north')
```

Actions on a choice edge run before the engine advances to the target node, so mutations are live immediately for the next node's condition checks.

### Sequencing

The `"sequence"` field on a choice edge controls how often that edge appears across repeated visits to the same Choice node.

| Value    | Behaviour                                                                                                                       |
| -------- | ------------------------------------------------------------------------------------------------------------------------------- |
| `"none"` | Always visible (default).                                                                                                       |
| `"once"` | Visible once; disappears after being taken. No flag name needed — managed automatically.                                        |
| `"cycle"` | Groups all `"cycle"` edges on this node into a rotation. One is shown per visit, cycling round-robin by visit count.           |
| `"random"` | Groups all `"random"` edges on this node. Exactly one is shown per visit, selected deterministically from the visit count. |

Sequencing is evaluated before conditions. If a sequenced edge is filtered out, the condition is not checked.

### Metadata

Any node can include a `"metadata"` object with arbitrary string key/value pairs. The engine ignores these — they are passed through to the presentation layer for use by the renderer, audio system, or cutscene director.

```json
{
  "id": "n0",
  "type": "talk",
  "speaker": "Elder",
  "text": "The seal must not be broken.",
  "next": "end",
  "metadata": {
    "voice": "elder_warning_01",
    "animation": "point_east",
    "camera": "close_up"
  }
}
```

---

## Writing Dialogue

### File layout

Create a `.json` file in `game/data/dialogue/`. It is picked up automatically at startup — no code change is needed.

```json
{
  "id": "my_npc",
  "nodes": [
    ...
  ]
}
```

The `"id"` must be unique across all dialogue files. It is the key used to connect an NPC entity to this graph via `DialogueRef`.

### Talk node

```json
{
  "id": "n0",
  "type": "talk",
  "speaker": "Guard",
  "text": "Halt. State your business.",
  "next": "n1"
}
```

`"next"` is the id of the node to advance to. Use `"end"` to close the conversation:

```json
{
  "id": "n1",
  "type": "talk",
  "speaker": "Guard",
  "text": "Move along.",
  "next": "end"
}
```

### Choice node

```json
{
  "id": "n1",
  "type": "choice",
  "choices": [
    { "label": "I'm a merchant.", "target": "n_merchant" },
    { "label": "Just passing through.", "target": "n_bye" }
  ]
}
```

`"label"` is shown to the player. `"target"` is the node to jump to on selection.

### Event node

An `event` node fires its actions and moves on — no text is shown and no input is required. Use it to separate logic from dialogue, or to trigger sound and quest effects as the conversation flows.

```json
{
  "id": "n_pay",
  "type": "event",
  "actions": ["gold -= 5", "play_sound('coin')"],
  "next": "n_thanks"
}
```

Chains of event nodes are flushed in a single frame — there is no perceptible delay between them.

### Conditional edges

Show an edge only when a condition is true:

```json
{ "label": "I have the key.", "target": "n_open", "condition": "has_key" }
```

Hide an edge when a flag is set:

```json
{ "label": "Can I enter?", "target": "n_enter", "condition": "!banned" }
```

Use a richer expression for numeric checks:

```json
{
  "label": "I need a room.",
  "target": "n_pay",
  "condition": "gold >= 5 && !paid_innkeeper"
}
```

Only show an edge on repeat visits (second conversation onward):

```json
{ "label": "Heard anything new?", "target": "n_news", "min_visits": 2 }
```

### Actions on edge traversal

```json
{
  "label": "I'll take the job.",
  "target": "n_accepted",
  "actions": ["quest_started = true"]
}
```

Multiple actions run in order:

```json
{
  "label": "I need a room.",
  "target": "n_pay",
  "condition": "gold >= 5 && !paid_innkeeper",
  "actions": ["gold -= 5", "paid_innkeeper = true"],
  "sequence": "once"
}
```

### Sequencing edges

Hide an edge after it has been taken once:

```json
{ "label": "What's happening here?", "target": "n_explain", "sequence": "once" }
```

Rotate through a pool of flavour remarks — only one is shown per visit, cycling:

```json
{
  "id": "n_greet",
  "type": "choice",
  "choices": [
    { "label": "[approach]", "target": "n_morning",   "sequence": "cycle" },
    { "label": "[approach]", "target": "n_afternoon",  "sequence": "cycle" },
    { "label": "[approach]", "target": "n_evening",    "sequence": "cycle" }
  ]
}
```

Show a random piece of gossip each visit (one from the pool):

```json
{
  "choices": [
    { "label": "Heard anything?", "target": "n_rumour_A", "sequence": "random" },
    { "label": "Heard anything?", "target": "n_rumour_B", "sequence": "random" },
    { "label": "Heard anything?", "target": "n_rumour_C", "sequence": "random" }
  ]
}
```

The random selection is deterministic per visit — re-opening the conversation without progressing shows the same choice.

---

## Patterns

### First-time vs. repeat greeting

Show a full introduction on first visit; fall back to a shorter one on return.

```json
{
  "id": "n0",
  "type": "choice",
  "choices": [
    { "label": "[approach]", "target": "n_first_meeting", "sequence": "once" },
    { "label": "[approach]", "target": "n_repeat",        "min_visits": 2    }
  ]
}
```

`"sequence": "once"` hides the first edge after it is taken. `min_visits: 2` hides the second until the node has been visited twice. Exactly one branch is active at any given visit.

### Gating on world state

Variables are set anywhere — another conversation, a quest, a combat outcome. Any dialogue can read them:

```json
{
  "label": "I dealt with the bandits.",
  "target": "n_reward",
  "condition": "bandits_cleared"
}
```

### Numeric resource checks

Spending gold with a condition guard and an action:

```json
{
  "label": "Buy the map (5 gold).",
  "target": "n_map_given",
  "condition": "gold >= 5",
  "actions": ["gold -= 5", "has_map = true"]
}
```

### Logic separated from dialogue

Use an `event` node between a choice and the response talk node when multiple things need to happen:

```json
{ "label": "I'll take the contract.", "target": "n_accept_event" },

{
  "id": "n_accept_event",
  "type": "event",
  "actions": ["start_quest('retrieve_sword')", "play_sound('quest_accepted')"],
  "next": "n_accept_talk"
},

{
  "id": "n_accept_talk",
  "type": "talk",
  "speaker": "Blacksmith",
  "text": "Good. Bring it back before nightfall.",
  "next": "end"
}
```

### One-shot information

Use `"sequence": "once"` to ensure the player can only ask for something once, without inventing a flag name:

```json
{ "label": "Tell me about the old ruins.", "target": "n_ruins", "sequence": "once" }
```

### Dead-end protection

If every edge on a Choice node is invisible, the engine closes the conversation gracefully rather than leaving the player stuck. Rely on this as a fallback, but prefer an explicit `"Never mind." → "end"` exit for authored control.

---

## Developer Notes

### Registering a dialogue graph

Drop a `.json` file in `game/data/dialogue/`. The `Registry` scans that directory at startup via `load_all()`. No C++ changes are needed.

### Connecting an NPC

Assign a `DialogueRef` component to the entity whose `"dialogue_id"` matches the graph's top-level `"id"`. The world update system handles the rest.

### Engine hook calls

Actions of the form `name(args...)` (e.g. `play_sound('coin')`, `start_quest('inn_stay')`) are collected by the dialogue system and returned from `system()` as a `std::vector<EventAction>`. The platform layer in `update.cpp` receives them. Wire up a dispatcher there to implement each hook name.

### Extending the action set

No C++ changes are needed to add new hook names — just call them from JSON. Only the platform dispatcher (`update.cpp` or a dedicated handler) needs a new case.

### Expression evaluation

Conditions are parsed at runtime by a recursive-descent evaluator in `engine/gameplay/dialogue/expr.hpp`. Action strings are validated at **load time** by the loader and re-parsed at runtime by `engine/gameplay/dialogue/action.hpp`. A bad action string causes a `LoadError` — fix it in JSON, not in C++.

---

## Reference

### Top-level fields

| Field       | Required | Description                                                              |
| ----------- | -------- | ------------------------------------------------------------------------ |
| `id`        | ✓        | Unique graph identifier. Must match the `DialogueRef` on the NPC entity. |
| `nodes`     | ✓        | Array of node objects. First entry is the entry point.                   |
| `variables` |          | Object of `"name": integer` defaults seeded into the flag store on start.|

### Node fields

| Field      | Required for     | Description                                                              |
| ---------- | ---------------- | ------------------------------------------------------------------------ |
| `id`       | all              | Unique within the graph.                                                 |
| `type`     | all              | `"talk"`, `"choice"`, `"event"`, or `"end"`.                             |
| `speaker`  | talk             | Display name shown above the text.                                       |
| `text`     | talk             | Body text. Supports `\n` for hard line breaks.                           |
| `next`     | talk, event      | Target node id, or `"end"`.                                              |
| `choices`  | choice           | Array of choice edge objects.                                            |
| `actions`  | event            | Array of action strings executed when the node is reached.               |
| `metadata` | —                | Optional object of string key/value pairs passed to the presentation layer. |

### Choice edge fields

| Field        | Required | Type     | Description                                                             |
| ------------ | -------- | -------- | ----------------------------------------------------------------------- |
| `label`      | ✓        | string   | Text shown to the player.                                               |
| `target`     | ✓        | string   | Target node id, or `"end"`.                                             |
| `condition`  |          | string   | Boolean expression; edge is hidden when false.                          |
| `actions`    |          | string[] | Action strings executed when the edge is taken.                         |
| `sequence`   |          | string   | `"none"` (default), `"once"`, `"cycle"`, or `"random"`.                 |
| `min_visits` |          | int ≥ 1  | Only visible after the containing Choice node has been visited N times. |

### Action string syntax

```
var = value          — assign (value: integer, true, or false)
var += value         — add
var -= value         — subtract
name(args...)        — engine hook call; args are quoted strings, identifiers, or integers
```

### Condition expression syntax

```
expr    := or_expr
or_expr := and_expr ('||' and_expr)*
and_expr:= not_expr ('&&' not_expr)*
not_expr:= '!' not_expr | cmp_expr
cmp_expr:= primary (('=='|'!='|'<'|'>'|'<='|'>=') primary)?
primary := integer | 'true' | 'false' | identifier | '(' expr ')'
```

Identifiers resolve to their integer value in the flag store (0 if absent). Comparing with `true`/`false` uses truthiness (non-zero = true), not exact equality.
