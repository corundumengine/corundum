# Controls / Input Contract

This documents every current player-input behavior precisely, in two
parts: a player-facing reference (what does what), and a technical
appendix (where each signal is bound and consumed in code). It doubles
as the seed data model for a future rebinding system — the code already
models input as a small set of **intents** (`corundum::input::Action`,
plus two dedicated non-Action signals) rather than raw keys, so this doc
maps directly onto what a settings screen or config-driven binding
system would need to expose. Building that system is not part of this
doc — see the note at the end of the technical appendix.

Re-run this doc as a review checklist whenever input behavior changes —
several entries below capture real, non-obvious behavior (e.g. Cancel's
hard-close semantics, Quit's three convergent sources) that's easy to get
wrong from memory.

## Part 1 — Player-Facing Reference

### Move

- **Bindings**: `W`/`A`/`S`/`D` or arrow keys (keyboard, held); left
  stick or D-pad (gamepad, held, deadzone 0.5).
- **Effect (Exploring mode)**: moves the player in the corresponding
  screen direction. Diagonals combine naturally (e.g. Up+Right).
  **Always cancels an active click-to-move path** — manual input takes
  priority over an in-progress path every frame it's held.
- **Effect (Dialogue mode, Choice prompt only)**: Up/Down instead
  navigate the highlighted choice, wrapping circularly (see Dialogue
  Choice Navigation below). Left/Right have no effect in dialogue.

### Click-to-Move

- **Binding**: left mouse click, on a tile (not on/aimed at an NPC — see
  Interact below for that case).
- **Effect**: computes a path via A* over the walkability graph from the
  player's current cell to the clicked tile, then walks it automatically.
  If the tile is unreachable (blocked by a wall, elevation cliff, or out
  of bounds), nothing happens — no path is queued. Canceled by any Move
  input.

### Interact / Talk to NPC

- **Bindings**: `Enter`, `Space` (keyboard); gamepad button 0 (A/Cross);
  **or** a left mouse click landing on the NPC's own tile.
- **Effect**: starts dialogue with an NPC that has a dialogue graph
  assigned, if the player is within `interact_radius` (`GameConfig`,
  default 2 tile-units) of it.
- **Keyboard/gamepad vs. click — different gating, deliberately**:
  a keyboard/gamepad press has no "aim" concept, so it triggers on
  proximity alone — the first interactable NPC within radius, in entity
  iteration order (not necessarily the nearest one, if more than one NPC
  happens to be in range at once). A mouse click *does* have a position,
  so it additionally
  requires the click to land on the NPC's own tile; clicking elsewhere
  while merely standing near an NPC starts a walk instead, not a
  conversation. (This is the fix for the "click-to-move near an NPC
  accidentally started dialogue" bug — before, a click's aim was ignored
  entirely.)

### Cancel / Close Dialogue

- **Bindings**: `Escape` (keyboard); gamepad button 1 (B/Circle).
- **Effect**: has no effect outside dialogue. Inside dialogue, it's a
  **hard close** — immediately ends the conversation from *either* a
  plain dialogue line or a Choice prompt. It does not "back up one step"
  or cancel just the current choice; the whole conversation ends.

### Dialogue Advance

- **Bindings**: same as Interact (`Enter`/`Space`/gamepad A/mouse click)
  — one shared action, context-dependent.
- **Effect**: on a plain dialogue line, advances to the next line. On a
  Choice prompt, confirms whichever choice is currently highlighted (see
  below) and advances along that branch.
- **Note**: because a mouse click also raises this same action, **clicking
  anywhere on screen during dialogue also advances the current line or
  confirms the current choice** — there's no aim requirement inside
  dialogue (unlike Interact, which only applies to the click that *starts*
  a conversation). This is existing, currently-unchanged behavior — worth
  a deliberate decision later (keep as click-to-advance, or restrict to a
  dialogue-box click target), not altered by this pass.

### Dialogue Choice Navigation

- **Bindings**: Move Up / Move Down (same keys/stick as world movement).
- **Effect**: moves the highlighted choice up or down the visible list,
  wrapping circularly (moving down from the last choice wraps to the
  first, and vice versa). **Not** number keys.

### Quit

- **Bindings**: `Q` (keyboard); gamepad button 7 (Start); the OS window
  close button.
- **Effect**: exits the game. All three sources are equivalent — they set
  the same underlying signal, checked once per frame in the main loop.

### Zoom

- **Bindings**: mouse scroll wheel (one wheel notch per step); `=`/`-`
  (keyboard, held, continuous); gamepad L2/R2 analog triggers (held,
  continuous — half-press or further counts as held).
- **Effect**: adjusts the camera's zoom level between `min_zoom` and
  `max_zoom` (`GameConfig`, default 0.5–3.0). Scroll up / `=` / R2 zooms
  in; scroll down / `-` / L2 zooms out.
- **Anchor point — different for mouse vs. keyboard/gamepad,
  deliberately**: scroll-wheel zoom keeps the world point *under the
  mouse cursor* visually fixed (zooming toward/away from whatever you're
  pointing at). Keyboard/gamepad zoom has no cursor to aim with, so it
  anchors on the screen center instead.
- **Note**: zoom is a pure camera-level transform — it does not change
  tile size, collision geometry, or walkability; only what fraction of
  the world is visible on screen.

## Part 2 — Technical Appendix (for future rebinding work)

Everything funnels through `corundum::input::InputState`
(`engine/include/corundum/input/actions.hpp`): a `held`/`pressed` bitset
pair over the `Action` enum (`MoveUp`, `MoveDown`, `MoveLeft`,
`MoveRight`, `Select`, `Cancel`, `Quit`, `ZoomIn`, `ZoomOut`), plus three
signals that deliberately sit *outside* the `Action` enum because they
carry information no discrete action has: `mouse_x`/`mouse_y`
(continuous cursor position), `mouse_click_pressed` (a one-shot "the
player clicked a screen point" flag, distinct from `Select`), and
`scroll_delta_y` (accumulated wheel delta this poll cycle — "how much",
not just "did it happen", so it can't be a discrete `Action` either).

### Binding tables (where defaults are declared)

All in `engine/src/platform/glfw/input_translator.cpp`:

| Table | Maps | Entries |
|---|---|---|
| `k_default_bindings` | GLFW key → `Action` | WASD/arrows → Move*; Enter/Space → Select; Escape → Cancel; Q → Quit; `=` → ZoomIn; `-` → ZoomOut |
| `k_button_bindings` | mapped gamepad button (`GLFW_GAMEPAD_BUTTON_*`) → `Action` | A → Select; B → Cancel; Start → Quit |
| `k_mouse_bindings` | mouse button → `Action` | Left click → Select |
| (inline in `poll_joystick`) | stick/d-pad/trigger axes → `Action` | left stick + d-pad → Move*, deadzone 0.5; L2 trigger → ZoomOut, R2 trigger → ZoomIn, threshold 0.0 |

Zoom is bound to the analog triggers (L2/R2), not the shoulder bumpers —
GLFW's mapped gamepad API reports triggers as axes
(`GLFW_GAMEPAD_AXIS_LEFT_TRIGGER`/`RIGHT_TRIGGER`, resting at -1.0 and
reading +1.0 fully pressed), not buttons, so they're handled alongside
the stick/d-pad axis logic in `poll_joystick()` rather than in
`k_button_bindings`.

`mouse_click_pressed` is **not** in a binding table — it's raised
directly in `translate_mouse_button()` (`input_translator.cpp`) whenever
the left button transitions to pressed, independent of the `Select`
binding above (both fire from the same physical click, deliberately).
`scroll_delta_y` similarly bypasses the binding tables — it's raised in
`translate_scroll()` from GLFW's scroll callback, since a continuous
signed magnitude has no discrete `Action` to bind to.

`poll_joystick()` uses GLFW's *mapped* gamepad API
(`glfwJoystickIsGamepad()` + `glfwGetGamepadState()`), not raw
`glfwGetJoystickButtons()`/`glfwGetJoystickAxes()` indices. Raw button
order varies per controller/platform (one tested controller reported its
Y button at raw index 4 and R1 at raw index 7 — nothing like the
originally-assumed Xbox layout), whereas `GLFW_GAMEPAD_BUTTON_*`
constants are normalized against the SDL gamepad database and stay
correct across devices. A controller with no SDL gamepad-DB entry is
silently ignored rather than bound to guessed raw indices.

A future rebinding system's most natural first step: replace these four
compile-time tables with the same shape loaded from a config file (the
`{key/button, Action}` pair structure already matches what a settings
UI would need to edit and persist) — no structural redesign required,
just a loader. **Not built as part of this doc.**

### Consumption sites (where each signal drives behavior)

| Signal | Consumed at | Behavior |
|---|---|---|
| `Action::MoveUp/Down/Left/Right` | `physics_sys.cpp::apply_input()` | World movement |
| `Action::MoveUp/Down/Left/Right` | `physics_sys.cpp::update_player()` | Cancels an active click-to-move path |
| `Action::MoveUp/Down` | `dialogue/system.cpp::system()` (`NodeType::Choice`) | Choice cursor navigation |
| `mouse_click_pressed` | `physics_sys.cpp::update_player()` | Queues a click-to-move path via `find_path()` |
| `Action::Select` | `dialogue_system.cpp::try_interact()` | Starts dialogue (proximity-only for keyboard/gamepad; proximity **and** click-aimed-at-NPC-tile for a click — see `mouse_click_pressed` check there) |
| `Action::Select` | `dialogue/system.cpp::system()` (`NodeType::Talk`) | Advances the line |
| `Action::Select` | `dialogue/system.cpp::system()` (`NodeType::Choice`) | Confirms the highlighted choice |
| `Action::Cancel` | `dialogue/system.cpp::system()` (`Talk` and `Choice`) | Hard-closes dialogue (`state.reset()`) |
| `Action::Quit` | `engine.cpp`'s main loop | Sets `engine.quit` and closes the window |
| `scroll_delta_y` | `world/update.cpp::update_zoom()` | `apply_zoom()`, anchored on the mouse cursor |
| `Action::ZoomIn/ZoomOut` (held) | `world/update.cpp::update_zoom()` | `apply_zoom()`, anchored on the screen center, rate-limited by `k_zoom_rate_per_sec` and `dt` |

`Action::Quit`'s three sources (Q key, gamepad Start, OS window-close
button) all write the *same* `InputState::held` bit — the window-close
callback (`glfw_window.cpp`) sets it exactly like a key press would, so
there's no separate immediate-exit path for player input. (A *different*,
non-input-triggered exit path exists for unrecoverable map-load failures
in `transition.cpp`, unrelated to any of the above.)

No manual camera pan, inventory, or pause-menu input exists yet — those
aren't gaps in this doc, they're gaps in the game. Camera zoom (above)
is the one exception: the camera itself is still a pure follow-cam with
no player-controlled panning.
