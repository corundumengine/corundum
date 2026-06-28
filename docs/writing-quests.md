# Writing Quests

Quests are defined as JSON files in the `data/quests/` directory. The engine loads them all at startup. This guide covers everything you need to write and wire up a quest, from a simple fetch task to a multi-ending moral choice.

---

## How quests work

A quest is a sequence of **stages**. Progress is stored as a single integer in the engine's flag store under the key `quest.{id}`. The value of that integer tells you which stage the player is currently in:

| Flag value                      | Meaning                                       |
| ------------------------------- | --------------------------------------------- |
| `0` (absent)                    | Quest not yet started                         |
| Positive integer                | Quest is active; matches a stage's `sequence` |
| Any resolved stage's `sequence` | Quest is over                                 |

Everything else — dialogue conditions, journal display, NPC reactions — reads directly from this one integer. No separate tracking needed.

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

- **`type`** — must be `"quest"`. Identifies the file to tools and the engine loader.
- **`id`** — machine-readable identifier, used in flag keys and dialogue actions. Snake_case, unique across all quests.
- **`name`** — shown in the journal.
- **`description`** — a brief premise shown at the top of the journal entry.
- **`stages`** — ordered list of stages. Every quest needs at least one stage with `"resolved": true`.

---

## Stages

Each stage represents a point in the quest's progress.

| Field        | Type    | Required | Description                                                                              |
| ------------ | ------- | -------- | ---------------------------------------------------------------------------------------- |
| `name`       | string  | yes      | Identifier used in dialogue actions (e.g. `"start"`, `"return"`, `"failed"`)             |
| `sequence`   | integer | yes      | Written to the flag store. Must be > 0 and unique within the quest.                      |
| `resolved`   | boolean | no       | `true` marks this stage as an ending. Defaults to `false`.                               |
| `failed`     | boolean | no       | `true` marks this stage as a failure ending. Implies `resolved`. Defaults to `false`.    |
| `objectives` | array   | yes      | List of objectives shown in the journal while this stage is active. Empty array is fine. |

### Stage sequences

Stage sequences do not need to be consecutive — spacing them out (`1, 10, 20`) leaves room to insert stages later without renumbering. Whatever sequence you choose, that number is what goes into the flag store.

### Resolved stages

Any stage can be marked `"resolved": true`. A quest is considered over when its flag matches any resolved stage. You can have as many resolved stages as you have endings — one for success, one for failure, one for each moral branch. See [Multiple endings](#multiple-endings) below.

---

## Objectives

Objectives are the lines displayed in the journal while a stage is active.

```json
{
  "text": "Find the lost sword in the dungeon",
  "done_condition": "sword_obtained >= 1"
}
```

| Field            | Type   | Required | Description                                                       |
| ---------------- | ------ | -------- | ----------------------------------------------------------------- |
| `text`           | string | yes      | The line shown in the journal                                     |
| `done_condition` | string | no       | A flag expression; when true, the objective is shown as completed |

`done_condition` uses the same expression syntax as dialogue conditions: `flag_name >= value`, `flag_name == value`, etc. The objective is checked-off automatically in the journal — it does **not** advance the quest. Quest advancement is always explicit, via dialogue actions.

If `done_condition` is absent, the objective never shows a checkmark (useful for open-ended tasks).

---

## Wiring quests to dialogue

Quests are started and advanced by dialogue actions. No code changes are needed — the dialogue system already supports this.

### Starting a quest

On a dialogue edge or event node:

```json
{ "actions": ["quest_start(\"find_sword\")"] }
```

This sets `quest.find_sword` to the value of the first stage. If the quest is already active, this is a no-op.

### Advancing a quest

```json
{ "actions": ["quest_advance(\"find_sword\", \"return\")"] }
```

This sets `quest.find_sword` to the sequence of the stage with `"name": "return"`. You can advance to any stage, including resolved ones. If the stage name doesn't exist, nothing happens (a warning is printed in debug builds).

### Checking quest state in dialogue

Use named condition helpers on dialogue edges to gate choices or node visibility:

```json
{ "condition": "quest_started(find_sword)" }
{ "condition": "quest_resolved(find_sword)" }
{ "condition": "quest_at(find_sword, complete_helped)" }
```

| Helper                           | Meaning                            |
| -------------------------------- | ---------------------------------- |
| `quest_started(quest_id)`        | Quest has been started (any stage) |
| `quest_resolved(quest_id)`       | Quest is over (any ending)         |
| `quest_failed(quest_id)`         | Quest ended in failure             |
| `quest_at(quest_id, stage_name)` | Quest is at a specific named stage |

`quest_at` is the preferred way to check a specific outcome — it uses stage names rather than sequence numbers, so your dialogue stays readable even if stages are renumbered.

Raw flag conditions still work and are useful for range checks:

```json
{ "condition": "quest.find_sword >= 2" }
```

This is how NPCs know whether to acknowledge a quest, offer a reward, or change their greeting.

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

Typical dialogue flow:

1. Player talks to the blacksmith → dialogue fires `quest_start("find_sword")`
2. Player finds the sword → `sword_obtained` flag is set elsewhere (item pickup, another dialogue)
3. Player returns → blacksmith's dialogue checks `quest.find_sword >= 1`, then advances via `quest_advance("find_sword", "return")`
4. Player accepts the reward → `quest_advance("find_sword", "complete")`

---

## Multiple endings

When a quest can end different ways, give each ending its own resolved stage:

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

Both `complete_helped` and `complete_betrayed` end the quest. Downstream dialogue distinguishes them by name:

```json
{ "condition": "quest_at(find_sword, complete_helped)" }
{ "condition": "quest_at(find_sword, complete_betrayed)" }
```

The ending stage's name is the outcome record. No separate outcome field is needed.

---

## Organic discovery (skipping the start stage)

Sometimes a quest begins not through a conversation but through a world event — the player finds a body, picks up a letter, enters a forbidden room. In these cases you can advance directly to any stage without calling `quest_start` first:

```json
{ "actions": ["quest_advance(\"find_sword\", \"start\")"] }
```

This sets the flag to the `start` stage's sequence even if the quest hasn't been started. The result is identical to calling `quest_start`, but it can be attached to any trigger in the world, not just an NPC conversation.

---

## Failure states

A failed quest is a resolved stage marked with `"failed": true`. You do not need to also write `"resolved": true` — the engine sets it automatically.

```json
{
  "name": "failed",
  "sequence": 99,
  "failed": true,
  "objectives": [{ "text": "The merchant was killed." }]
}
```

Wire it to whatever event causes failure — a death trigger, a timer expiring, a dialogue choice. The journal can display failed quests differently from completed ones, and NPCs can react to failure specifically:

```json
{ "condition": "quest_failed(escort_merchant)" }
```

---

## Naming conventions

| Thing          | Convention        | Example                                        |
| -------------- | ----------------- | ---------------------------------------------- |
| Quest `id`     | `snake_case`      | `find_sword`, `escort_merchant`                |
| Stage `name`   | `snake_case`      | `start`, `return`, `complete_helped`, `failed` |
| Flag store key | Auto-generated    | `quest.find_sword`                             |
| Quest file     | `{quest_id}.json` | `find_sword.json`                              |

---

## Validation rules

The loader rejects quests that violate any of these rules and prints a warning to stderr. The rest of the quests still load.

- `type` must be present and equal to `"quest"`
- Quest `id` and `name` must be non-empty
- All stage `name` values must be unique within the quest
- All stage `sequence` fields must be > 0 and unique within the quest
- At least one stage must have `"resolved": true`

---

## Quick reference

```
Start a quest:       quest_start("quest_id")
Advance a quest:     quest_advance("quest_id", "stage_name")

Check started:       quest_started(quest_id)
Check resolved:      quest_resolved(quest_id)
Check failed:        quest_failed(quest_id)
Check named stage:   quest_at(quest_id, stage_name)
Check progress:      quest.quest_id >= sequence   (raw flag condition)
```
