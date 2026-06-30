# Localization Plan — Issue Responses

## 1. Missing unit tests

Add `tests/test_localization.cpp` following the existing test patterns (`test_quest.cpp`).

**What to test:**

| Test case | What it proves |
|-----------|---------------|
| Load valid locale JSON, verify `empty()` is false | Happy path — file I/O + parse |
| `get()` returns mapped value for existing key | Correct lookup |
| `get()` returns fallback for missing key | Fallback semantics |
| `empty()` returns true before any load | Initial state |
| `load()` returns error for malformed JSON | Error propagation |
| Non-string JSON values are silently skipped | Robustness against rogue values |

**CMake** — no changes needed. `test_localization.cpp` is added to `corundum_tests` sources in `tests/CMakeLists.txt`. Tests already link `engine` and `doctest::doctest`.

**Pattern** (from `test_quest.cpp`):
```cpp
#include <doctest/doctest.h>
#include <corundum/core/localization.hpp>

TEST_CASE("LocaleRegistry: get returns mapped value") {
  auto tmp = std::filesystem::temp_directory_path() / "locale_test";
  std::filesystem::create_directories(tmp);
  {
    std::ofstream f(tmp / "en.json");
    f << R"({"dialogue.greet.n0.text": "Hello"})";
  }
  corundum::core::LocaleRegistry reg;
  auto result = reg.load(tmp / "en.json");
  CHECK(result.has_value());
  CHECK(reg.get("dialogue.greet.n0.text", "Hi") == "Hello");
  CHECK(reg.get("nonexistent.key", "Fallback") == "Fallback");
  std::filesystem::remove_all(tmp);
}
```

---

## 2. `locale` field placement

`locale` is a language tag (`"en"`, `"fr"`), not a path. Moving it to `GameConfig` prevents semantic confusion.

**`engine/include/corundum/core/game_config.hpp`** changes:

```cpp
struct ResourcePaths {
  // ... existing fields ...
  std::string localization_dir{"data/localization"};  // was proposed — belongs here
};

struct GameConfig {
  // ... existing fields ...
  std::string locale{"en"};                           // language tag — NOT in ResourcePaths
  // ...
};
```

**`engine/src/core/game_config.cpp`** — parse both using existing `get_nonempty_string()`:

```cpp
// In load_game_config(), after window_title:
{
  auto res = get_nonempty_string(j, "locale", cfg.locale, path);
  if (!res) return std::unexpected(res.error());
  cfg.locale = std::move(*res);
}
{
  auto res = get_nonempty_string(j, "localization_dir", cfg.paths.localization_dir, path);
  if (!res) return std::unexpected(res.error());
  cfg.paths.localization_dir = std::move(*res);
}
```

---

## 3. Locale reachability in `dialog_box_update()`

The plan says "pass `&engine.locale`" but `dialog_box_update()` doesn't reach `Engine`. The call chain is:

```
engine.render                   (Engine member)
  → RenderState::dialog_box     (DialogBoxState)
    → dialog_box_update(ds, ...)  called from render_sys.cpp
      → build_layout(...)          called inside dialog_box_update
```

**Recommended approach**: store a `const core::LocaleRegistry*` in `DialogBoxState`. Set it once during engine `initialize()`. The pointer is cheap, nullable, and avoids widening every function signature in the call chain.

### Changes

**`engine/include/corundum/gameplay/ui/dialog_box.hpp`** — add field to `DialogBoxState`:

```cpp
struct DialogBoxState {
  DialogBoxStyle style{};
  NinePatchBorder border{};
  bool visible{false};
  std::string last_node_id{};
  float last_panel_w{0.f};
  std::optional<DialogLayout> layout{};
  const core::LocaleRegistry *locale{nullptr};  // <-- new
};
```

**`engine/src/engine.cpp`** — in `initialize()`, after loading locale:

```cpp
engine.render.dialog_box.locale = &engine.locale;
```

**`engine/src/gameplay/ui/dialog_box.cpp`** — pass to `build_layout()`:

```cpp
ds.layout = build_layout(state, flags, ds.style.margin, ds.style.panel_height_frac,
                         ds.border.tile_w, viewport, measure, ds.locale);
```

**`engine/include/corundum/gameplay/ui/dialog_layout.hpp`** — add locale param to `build_layout()`:

```cpp
template <typename MeasureFn>
[[nodiscard]] DialogLayout build_layout(const gameplay::dialogue::State &state,
                                        const gameplay::FlagStore &flags,
                                        float margin, float panel_height_frac,
                                        int border_tile_w, core::math::Vec2 viewport,
                                        MeasureFn measure,
                                        const core::LocaleRegistry *locale = nullptr) {
  // ... existing layout computation ...

  // Speaker
  if (locale) {
    auto key = std::format("dialogue.{}.speaker", state.graph->graph_id);
    layout.speaker = locale->get(key, state.graph->speaker);
  } else {
    layout.speaker = state.graph->speaker;
  }

  // Body text
  if (node->type == gameplay::dialogue::NodeType::Talk) {
    std::string_view text = node->text;
    if (locale) {
      auto key = std::format("dialogue.{}.{}.text", state.graph->graph_id, node->id);
      text = locale->get(key, node->text);
    }
    layout.body_lines = gameplay::wrap_text(text, text_w, measure);
  }

  // Choice labels (see issue #4 for the correct index)
  if (node->type == gameplay::dialogue::NodeType::Choice) {
    // ...
    for (const std::size_t idx : layout.choice_indices) {
      std::string_view label = node->choices[idx].label;
      if (locale) {
        auto key = std::format("dialogue.{}.{}.choice.{}.label",
                               state.graph->graph_id, node->id, idx);
        label = locale->get(key, node->choices[idx].label);
      }
      auto lines = gameplay::wrap_text(label, choice_w, measure);
      layout.choice_lines.push_back(lines.empty() ? std::string{} : std::move(lines.front()));
    }
  }
}
```

The `nullptr` default keeps all existing callers (tests, other tools) compiling without changes.

---

## 4. Choice label key uses wrong index variable

The plan's pseudocode shows:
```
dialogue.<graph_id>.<node_id>.choice.<i>.label
```
where `i` is described as "original index in `node->choices`". This is correct in intent, but the iteration in `build_layout()` uses `layout.choice_indices` (returned by `visible_choices()`) — not a sequential counter.

**Wrong** — using a loop counter:
```cpp
for (std::size_t i = 0; i < layout.choice_indices.size(); ++i) {
  auto idx = layout.choice_indices[i];
  auto key = std::format("dialogue.{}.{}.choice.{}.label", ..., i); // BUG: i is display index
}
```

**Correct** — using the original index from `choice_indices`:
```cpp
for (const std::size_t idx : layout.choice_indices) {
  auto key = std::format("dialogue.{}.{}.choice.{}.label",
                         state.graph->graph_id, node->id, idx);  // idx is the original position
}
```

The key `idx` is the index into `node->choices[]`, which is stable across saves. This is the same index used in save flags and `visible_choices()`. Display position changes (filtered by conditions/sequence modes) must never alter the key.

---

## 5. Missing `--force` flag on locextract

Add an optional `--force`/`-f` flag to overwrite an existing output file instead of warning.

**Usage:**
```sh
build/tools/locextract --dialogue data/npc --quests data/quests --out data/localization/en.json
  # Warns and exits if en.json already exists

build/tools/locextract --dialogue data/npc --quests data/quests --out data/localization/en.json --force
  # Overwrites silently
```

**Implementation sketch:**
```cpp
int main(int argc, char **argv) {
  bool force = false;
  // ... parse args ...
  if (std::filesystem::exists(out_path) && !force) {
    std::println(stderr, "[locextract] {} exists; use --force to overwrite", out_path.string());
    return 1;
  }
  // ... generate and write ...
}
```

**Update `tools/CMakeLists.txt`:**

```cmake
add_executable(locextract
    editors/locextract/main.cpp
)

target_link_libraries(locextract
    PRIVATE
        engine
        nlohmann_json::nlohmann_json
)

target_compile_options(locextract PRIVATE -Wall -Wextra -Wpedantic)
```

No `tools_common` or `tools_imgui_backend` dependency needed — `engine` pulls in only `nlohmann_json::nlohmann_json` (no platform layer).

---

## 6. Talesmith not addressed

Acceptable for initial infrastructure — the editor is a separate concern. However, note the following touch points when the editor is updated:

**Current behavior**: talesmith reads/writes `Node::text`, `ChoiceEdge::label`, `Graph::speaker`, `Quest::name`, `Quest::description`, `Objective::text` directly as source text in JSON files. It has no concept of a locale table.

**Future considerations:**

| Feature | Notes |
|---------|-------|
| Locale key display | Show the auto-derived key (e.g. `dialogue.innkeeper_intro.n0.text`) as a non-editable tooltip on each text field |
| Translation edit mode | Add a locale file selector; when active, text fields show the translated value and edits write to the locale JSON instead of the source file |
| `locextract` integration | Add an in-editor "Extract strings" button that calls `locextract` with the current project paths |
| Fallback highlighting | Highlight fields that have no locale entry (render in yellow/red in translation mode) |

None of these are required for the initial release. The locale pointer defaulting to `nullptr` in `build_layout()` means talesmith continues to work unchanged.
