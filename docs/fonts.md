# Fonts

Corundum loads up to three font families per game, one for each **role**. A role decides which typeface a given piece of text draws in; a family can supply separate files for regular, bold, italic and bold-italic.

---

## Roles

| Role | Used by |
|---|---|
| `dialogue` | Dialogue box, barter conversation text |
| `quest` | Journal, quest tracker, codex entries |
| `ui` | Menus, settings, HUD, inventory, toasts, debug overlay |

Every game must configure all three roles. A role's `regular` file is required; the other three styles are optional.

---

## `fonts` in `game.json`

Fonts live in `font_dir` (default `assets/fonts`). The `fonts` block names the files for each role, relative to `font_dir`:

```json
{
  "font_dir": "assets/fonts",
  "fonts": {
    "dialogue": {
      "regular": "dialogue-regular.ttf",
      "bold": "dialogue-bold.ttf",
      "italic": "dialogue-italic.ttf",
      "bold_italic": "dialogue-bold-italic.ttf"
    },
    "quest": {
      "regular": "quest-regular.ttf"
    },
    "ui": {
      "regular": "ui-regular.ttf"
    }
  }
}
```

| Key | Required | Description |
|---|---|---|
| `dialogue` | yes | Family used for dialogue text. |
| `quest` | yes | Family used for journal, quest tracker and codex text. |
| `ui` | yes | Family used for menus, HUD and every other screen. |
| `<role>.regular` | yes | The family's regular face. Must be non-empty. |
| `<role>.bold` | no | Bold face. |
| `<role>.italic` | no | Italic face. |
| `<role>.bold_italic` | no | Bold-italic face. |

Each family value must be an object, and any style that is present must name a non-empty file. A missing role, a non-object family, or an empty `regular` is a config error and the game fails to start; an empty optional style (for example `"bold": ""`) is also an error — omit the key instead.

---

## Missing styles

Bold, italic and bold-italic are optional. When a style file is absent or fails to load, that style renders with the closest face the family does provide, and a warning is logged at startup:

| Requested style | Falls back to |
|---|---|
| `bold` | `regular` |
| `italic` | `regular` |
| `bold_italic` | `bold`, else `italic`, else `regular` |

There is no synthetic bold or italic — a family without a bold file simply draws bold text in its regular face.

If a required `regular` file cannot be loaded, the game fails to start with an error naming the file.

---

## Sizes are role-independent

Font files are picked per role, but **sizes are not**. The existing size settings apply to every role and every style:

- `dialogue_render.font_size_body`, `font_size_speaker` and `font_size_prompt` from `game.json`
- the UI scale

So bold text is the same size as regular, and a quest font is not sized independently of the dialogue font. Writers should not expect per-role sizing.

---

## Emphasizing text

Talk text, choice labels, quest objectives and codex bodies accept inline markdown-lite markup (`*italic*`, `**bold**`, `***bold italic***`). The grammar is documented once per content type:

- [Writing dialogue — Emphasizing text](writing-dialogue.md#emphasizing-text)
- [Writing quests — Emphasizing text](writing-quests.md#emphasizing-text)

Markup uses the bold and italic faces of the role's family; when those files are missing, it falls back as described above.
