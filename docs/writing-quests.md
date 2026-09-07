# Writing Quests

Quests are JSON files in `data/quests/` (or wherever `quests_dir` points in `game.json`; it defaults to `data/quests`). The engine loads them all at startup.

---

## How quests work

A quest is a sequence of stages, and progress is one integer in the flag store under `quest.{id}`. That integer tells you which stage the player is on:

| Flag value | Meaning |
|---|---|
| `0` (absent) | Quest not yet started |
| A stage's `sequence` | Quest is active on that stage |
| A resolved stage's `sequence` | Quest is over (completed or failed) |

Dialogue conditions, the journal, NPC reactions: everything reads from this one integer. Nothing else to track.

### Lifecycle status

The engine derives a typed `quest::Lifecycle` from the flag value:

| Status | Meaning |
|---|---|
| `NotStarted` | The `quest.{id}` flag is absent or 0. |
| `Active` | On a stage that isn't resolved or failed. |
| `Completed` | On a resolved, non-failed stage. |
| `Failed` | On a failed stage (failed stages are also resolved). |

You rarely touch the enum when authoring. Dialogue conditions use the `quest_is_*` helpers instead (see [Quest condition helpers](#quest-condition-helpers)).

---

## A minimal quest

```json
{
  "type": "quest",
  "id": "find_sword",
  "name": "The Lost Sword",
  "description": "A blade of legend, lost in the old dungeon.",
  "stages": [
    {
      "name": "start",
      "sequence": 1,
      "objectives": [{ "text": "Find the lost sword in the dungeon" }]
    },
    {
      "name": "complete",
      "sequence": 2,
      "resolved": true,
      "objectives": []
    }
  ]
}
```

- **`type`:** must be `"quest"`. Tells tools and the loader what the file is.
- **`id`:** machine-readable identifier, used in flag keys and dialogue actions. Snake_case and unique across all quests.
- **`name`:** shown in the journal.
- **`description`:** a short premise shown at the top of the journal entry.
- **`stages`:** ordered list of stages. Every quest needs at least one `"resolved": true` stage.

---

## Stages

Each stage is one point in the quest's progress.

| Field | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Identifier used in dialogue actions (e.g. `"start"`, `"return"`, `"failed"`) |
| `sequence` | integer | yes | Written to the flag store. Must be > 0 and unique within the quest. |
| `resolved` | boolean | no | `true` marks the stage as an ending. Defaults to `false`. |
| `failed` | boolean | no | `true` marks the stage as a failure ending. Implies `resolved`. Defaults to `false`. |
| `objectives` | array | yes | Journal lines shown while this stage is active. An empty array is fine. |
| `advances_to` | array | no | Stage names this stage may legally advance to. Declared and load-validated only; see [Legal transitions](#legal-transitions-advances_to). |
| `auto_advance_to` | string | no | A single stage name to advance to once this stage's conditioned objectives are all done. |

### Stage sequences

Sequences don't need to be consecutive. Spacing them out (`1, 10, 20`) leaves room to insert stages later without renumbering. Whatever number you pick is what goes into the flag store.

### Resolved stages

Any stage can be marked `"resolved": true`. The quest is over once its flag matches a resolved stage, so you can have as many resolved stages as you have endings: success, failure, one per moral branch.

---

## Objectives

Objectives are the lines shown in the journal while a stage is active.

```json
{
  "text": "Find the lost sword in the dungeon",
  "done_condition": "sword_obtained >= 1"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `text` | string | yes | The line shown in the journal |
| `done_condition` | string | no | A flag expression; when true, the objective is shown as completed |

`done_condition` uses the same expression syntax as dialogue conditions: `flag_name >= value`, `quest_is_at(...)`, and so on. It's compiled once at load, and a malformed expression is a hard load error. The journal checks the objective off automatically.

An objective with a `done_condition` also feeds auto-advancement; one without is just display text.

### Auto-advancement

Give a stage `"auto_advance_to": "stage_name"` and the quest advances to that single stage once every objective that carries a `done_condition` evaluates true, in any order. A stage with no conditioned objectives never auto-advances, and neither does one without `auto_advance_to`.

```json
{
  "name": "interrogation",
  "sequence": 2,
  "objectives": [
    { "text": "Reveal the secret", "done_condition": "secret_learned >= 1" },
    { "text": "Take the notes", "done_condition": "notes_taken == 1" },
    { "text": "Threaten the spy", "done_condition": "spy_fired == 1" }
  ],
  "auto_advance_to": "ending_confrontation"
}
```

When all three objectives are done, the quest moves to `ending_confrontation`. Note that `auto_advance_to` names exactly one stage: a single target, not a list. If you need a branch, give each ending its own stage and have the dialogue that finishes the last objective `quest_advance` to the right one.

### Legal transitions: `advances_to`

The `advances_to` array declares which stages a stage may legally advance to:

```json
{
  "name": "search",
  "sequence": 2,
  "objectives": [],
  "advances_to": ["return", "failed"]
}
```

An empty `advances_to` (the default) keeps the old behaviour: any stage name is a valid target. When it's non-empty, the loader rejects any listed name that doesn't exist in the quest. `advances_to` is a declaration of intent, surfaced to authors and tools, not a hard runtime clamp: in debug builds a transition to a stage not listed here (and not the stage's `auto_advance_to`) prints a warning, but the transition still happens. Enforce it with dialogue conditions.

---

## Wiring quests to dialogue

Quests are started and advanced through dialogue actions. No code needed.

### Starting a quest

On a dialogue edge or event node:

```json
{ "actions": ["quest_start('find_sword')"] }
```

This sets `quest.find_sword` to the first stage's sequence. If the quest is already started, it's a no-op.

### Advancing a quest

```json
{ "actions": ["quest_advance('find_sword', 'return')"] }
```

This sets `quest.find_sword` to the sequence of the stage named `"return"`. You can advance to any stage, resolved ones included. If the stage name doesn't exist, nothing happens and a warning is printed.

### Quest condition helpers

| Helper | Meaning |
|---|---|
| `quest_is_started(quest_id)` | The `quest.<id>` flag is set (any stage, including completed) |
| `quest_is_resolved(quest_id)` | The quest is on a resolved stage (completed or failed) |
| `quest_is_failed(quest_id)` | The quest ended in failure |
| `quest_is_at(quest_id, stage_name)` | The quest is at a specific named stage |

```json
{ "condition": "quest_is_at(find_sword, complete_helped)" }
{ "condition": "quest_is_failed(escort_merchant)" }
```

### Raw flag conditions

Plain flag conditions still work and are handy for range checks:

```json
{ "condition": "quest.find_sword >= 2" }
```

The engine just reads the `quest.<id>` flag value.

---

## Stage history (`seen` flags)

Whenever a quest starts or advances into a stage, the engine also sets a `quest.{id}.seen.{stage}` flag, incrementing it each time that stage is entered. These are ordinary flags, so you can read one in a raw condition to confirm the player passed through a stage:

```json
{ "condition": "quest.find_sword.seen.start >= 1" }
```

Useful for catching skipped content, or gating dialogue on a stage the player reached earlier even after the quest moved on. There's no dedicated helper; just read the dotted key.

---

## A complete linear quest

```json
{
  "type": "quest",
  "id": "find_sword",
  "name": "The Lost Sword",
  "description": "A blade of legend, lost in the old dungeon.",
  "stages": [
    {
      "name": "start",
      "sequence": 1,
      "objectives": [
        {
          "text": "Find the lost sword in the dungeon",
          "done_condition": "sword_obtained >= 1"
        }
      ]
    },
    {
      "name": "return",
      "sequence": 2,
      "objectives": [
        {
          "text": "Return the sword to the blacksmith"
        }
      ]
    },
    {
      "name": "complete",
      "sequence": 3,
      "resolved": true,
      "objectives": []
    }
  ]
}
```

A typical flow:

1. The player talks to the blacksmith, whose dialogue fires `quest_start('find_sword')`.
2. The player finds the sword, and something else sets the `sword_obtained` flag (an item pickup, another dialogue).
3. The player returns; the blacksmith's dialogue checks `quest_is_at(find_sword, return)` or `quest.find_sword >= 1`, then calls `quest_advance('find_sword', 'return')`.
4. The player takes the reward, and the dialogue calls `quest_advance('find_sword', 'complete')`.

---

## Multiple endings

When a quest can end more than one way, give each ending its own resolved stage:

```json
{
  "type": "quest",
  "id": "find_sword",
  "name": "The Lost Sword",
  "description": "A blade of legend, lost in the old dungeon.",
  "stages": [
    {
      "name": "start",
      "sequence": 1,
      "objectives": [
        {
          "text": "Find the lost sword in the dungeon",
          "done_condition": "sword_obtained >= 1"
        }
      ]
    },
    {
      "name": "return",
      "sequence": 2,
      "objectives": [{ "text": "Return the sword to the blacksmith" }]
    },
    {
      "name": "complete_helped",
      "sequence": 3,
      "resolved": true,
      "objectives": []
    },
    {
      "name": "complete_betrayed",
      "sequence": 4,
      "resolved": true,
      "objectives": []
    }
  ]
}
```

Both `complete_helped` and `complete_betrayed` end the quest. Downstream dialogue tells them apart by name:

```json
{ "condition": "quest_is_at(find_sword, complete_helped)" }
{ "condition": "quest_is_at(find_sword, complete_betrayed)" }
```

The ending stage's name is the outcome record. No separate outcome field needed.

---

## Organic discovery (skipping the start stage)

Not every quest starts in conversation. The player might find a body, pick up a letter, or walk into a forbidden room. In those cases you can advance straight to any stage without calling `quest_start` first:

```json
{ "actions": ["quest_advance('find_sword', 'start')"] }
```

This sets the flag to the `start` stage's sequence even though the quest was never formally started. The effect is the same as `quest_start`, but you can attach it to any world trigger, not just an NPC conversation.

---

## Failure states

A failed quest is a resolved stage marked `"failed": true`. You don't need to also write `"resolved": true`; the engine sets it for you.

```json
{
  "name": "failed",
  "sequence": 99,
  "failed": true,
  "objectives": [{ "text": "The merchant was killed." }]
}
```

Wire it to whatever causes the failure: a death trigger, an expired timer, a dialogue choice. The journal can show failed quests differently from completed ones, and NPCs can react to failure specifically:

```json
{ "condition": "quest_is_failed(escort_merchant)" }
```

---

## Naming conventions

| Thing | Convention | Example |
|---|---|---|
| Quest `id` | `snake_case` | `find_sword`, `escort_merchant` |
| Stage `name` | `snake_case` | `start`, `return`, `complete_helped`, `failed` |
| Flag store key | Auto-generated | `quest.find_sword` |
| Quest file | `{quest_id}.json` | `find_sword.json` |
| Condition helpers | `quest_is_<state>` | `quest_is_started`, `quest_is_at` |

---

## Validation rules

The loader rejects a quest that breaks any of these and prints a warning to stderr; the remaining quests still load.

- `type` must be present and equal to `"quest"`
- `id`, `name`, and `description` must be present; `id` and `name` must be non-empty
- Stage `name`s must be unique within the quest
- Stage `sequence`s must be > 0 and unique within the quest
- At least one stage must have `"resolved": true`
- Stage names in `"advances_to"` must all exist in the same quest
- `"auto_advance_to"`, when present, must be a non-empty stage name that exists in the same quest
- Every `done_condition` must compile

---

## Quick reference

```
Quest condition helpers:
  quest_is_started(quest_id)    - quest flag is set (any stage)
  quest_is_resolved(quest_id)   - quest is over (completed or failed)
  quest_is_failed(quest_id)     - quest ended in failure
  quest_is_at(quest_id, stage)  - quest is at a specific named stage

Actions:
  Start a quest:          quest_start('quest_id')
  Advance a quest:        quest_advance('quest_id', 'stage_name')

Stage fields:
  name             - identifier used in actions
  sequence         - integer written to quest.<id>
  resolved         - true marks an ending
  failed           - true marks a failure ending (implies resolved)
  objectives[]     - journal lines; text + optional done_condition
  advances_to[]    - legal advance targets (declared, load-validated)
  auto_advance_to  - single stage name, advances when all objectives done

Progress:            quest.quest_id >= sequence   (raw flag condition)
Stage history:       quest.quest_id.seen.stage >= 1   (set on entry to a stage)
```
