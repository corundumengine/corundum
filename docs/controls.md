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
hard-close semantics, Quit's split between a player action and an OS
event) that's easy to get
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
- **Effect**: computes a path via A\* over the walkability graph from the
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
  happens to be in range at once). A mouse click _does_ have a position,
  so it additionally
  requires the click to land on the NPC's own tile; clicking elsewhere
  while merely standing near an NPC starts a walk instead, not a
  conversation. (This is the fix for the "click-to-move near an NPC
  accidentally started dialogue" bug — before, a click's aim was ignored
  entirely.)

### Cancel / Close Dialogue

- **Bindings**: `Escape` (keyboard); gamepad button 1 (B/Circle).
- **Effect**: has no effect outside dialogue. Inside dialogue, it's a
  **hard close** — immediately ends the conversation from _either_ a
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
  dialogue (unlike Interact, which only applies to the click that _starts_
  a conversation). This is existing, currently-unchanged behavior — worth
  a deliberate decision later (keep as click-to-advance, or restrict to a
  dialogue-box click target), not altered by this pass.

### Dialogue Choice Navigation

- **Bindings**: Move Up / Move Down (same keys/stick as world movement).
- **Effect**: moves the highlighted choice up or down the visible list,
  wrapping circularly (moving down from the last choice wraps to the
  first, and vice versa). **Not** number keys.

### Quit

- **Bindings**: `Q` (keyboard); the OS window close button (a platform
  event, not an `Action`).
- **Effect**: exits the game. The keyboard binding raises `Action::Quit`;
  the window close button raises `PlatformEvents::quit_requested`. Both
  are checked once per frame in the main loop. Gamepad Start is
  deliberately unbound — what Start does is a game-design decision, not a
  hard-wired quit.

### Menu Hub (Inventory / Journal / Codex / Map)

- **Bindings**: gamepad Y/Triangle (Hub); keyboard `I` / `J` / `C` / `M`
  select a tab directly.
- **Effect**: the four menu screens live behind one hub screen with top-level
  tabs. On a gamepad, **Hub** opens the hub on the last tab you used and
  closes it when a tab is already open; on a keyboard, each of `I`/`J`/`C`/`M`
  opens or switches to its own tab (and closes it when it is already on top).
  `Cancel` closes whichever tab is showing.
- **Tab switching**: the shoulder bumpers `L1`/`R1` cycle Inventory → Journal →
  Codex → Map and wrap. On the keyboard `[` / `]` do the same.
- **Scope**: the hub opens only from free-roam (Exploring) or when a tab is
  already on top. It does not open over dialogue, a transition prompt, the
  pause menu, the settings screen, loot or barter. Loot and barter are opened
  from dialogue interactions, not from the hub.
- **Note**: the four screens below describe each tab's own navigation; the
  hotkey/button above reaches it.

### Inventory

- **Bindings**: `I` (keyboard) opens the hub on the Inventory tab; gamepad Hub
  (Y) then bumpers to reach it.
- **Effect**: opening it pauses the player; `I` again or `Esc` closes it. Move
  Up/Down move the highlighted item row, wrapping circularly. The row list is
  built once when the tab opens (the inventory is read-only while paused).

### Journal (Quest Log)

- **Bindings**: `J` (keyboard) opens the hub on the Journal tab; gamepad Hub
  (Y) then bumpers.
- **Effect**: opening it pauses the player; `J` again or `Esc` closes it. Move
  Up/Down move the highlighted quest row (Active first, then Completed, then
  Failed), wrapping circularly.

### Codex (Lore)

- **Bindings**: `C` (keyboard) opens the hub on the Codex tab; gamepad Hub (Y)
  then bumpers.
- **Effect**: opening it pauses the player; `C` again or `Esc` closes it. Move
  Up/Down move the highlighted entry (grouped by category); the selected
  entry's body is wrapped in the detail pane and the scroll wheel scrolls it.
  Only entries unlocked via `unlock_codex`/a `codex.<id>` flag appear.

### Map / Fast Travel

- **Bindings**: `M` (keyboard) opens the hub on the Map tab; gamepad Hub (Y)
  then bumpers.
- **Effect**: opening it pauses the player; `M` again or `Esc` closes it. Move
  Up/Down move the highlighted destination (only discovered
  `location.<id>.discovered` locations appear); Enter/Space/gamepad A
  fast-travels there. Travelling to the zone you are already in is a no-op.

### Loot and Barter (contextual)

- **Opened from dialogue**, not by a dedicated key: `open_container('id')`
  opens the two-pane loot screen and `open_shop('id')` opens the barter screen.
- **Loot**: Left/Right switch the container/player pane; Up/Down move the row;
  Enter/Space/gamepad A moves one unit of the highlighted item to the other
  holder; `Esc` closes.
- **Barter**: Left/Right or TabNext/TabPrev switch the Buy/Sell tab; Up/Down
  move the row; Enter/Space/gamepad A buys (spending gold, with a
  `rep.<faction>` discount) or sells at the shop's `buy_rate`; `Esc` closes.

### Zoom

- **Bindings**: mouse scroll wheel (one wheel notch per step); `=`/`-`
  (keyboard, held, continuous); gamepad L2/R2 analog triggers (held,
  continuous — half-press or further counts as held).
- **Effect**: adjusts the camera's zoom level between `min_zoom` and
  `max_zoom` (`GameConfig`, default 0.5–3.0). Scroll up / `=` / R2 zooms
  in; scroll down / `-` / L2 zooms out.
- **Anchor point — different for mouse vs. keyboard/gamepad,
  deliberately**: scroll-wheel zoom keeps the world point _under the
  mouse cursor_ visually fixed (zooming toward/away from whatever you're
  pointing at). Keyboard/gamepad zoom has no cursor to aim with, so it
  anchors on the screen center instead.
- **Note**: zoom is a pure camera-level transform — it does not change
  tile size, collision geometry, or walkability; only what fraction of
  the world is visible on screen.

## Part 2 — Technical Appendix (for future rebinding work)

Everything funnels through `corundum::input::InputState`
(`engine/include/corundum/input/actions.hpp`): a `held`/`pressed` bitset
pair over the `Action` enum (`MoveUp`, `MoveDown`, `MoveLeft`,
`MoveRight`, `Select`, `Cancel`, `Quit`, `ZoomIn`, `ZoomOut`,
`Inventory`, `Journal`, `QuickSave`, `QuickLoad`), plus three
signals that deliberately sit _outside_ the `Action` enum because they
carry information no discrete action has: `mouse_x`/`mouse_y`
(continuous cursor position), `mouse_click_pressed` (a one-shot "the
player clicked a screen point" flag, distinct from `Select`), and
`scroll_delta_y` (accumulated wheel delta this poll cycle — "how much",
not just "did it happen", so it can't be a discrete `Action` either).
The enum also carries the screen toggles `Inventory`, `Journal`, `Codex`,
`Map`, `Menu`, `TabNext`/`TabPrev`, and `Activate` (the world/UI "use this
thing" intent, distinct from the dialogue/confirm `Select`).

### Binding tables (where defaults are declared)

The default binding table is built by `default_bindings()`
(`engine/src/input/bindings.cpp`): each row pairs an `Action` with one
`PhysicalInput` (a `Key`, `MouseButton`, or `GamepadControl`). Movement is
WASD/arrows plus the left stick and D-pad; Select is Enter/Space, mouse-left
and gamepad A; Cancel is Escape and gamepad B; the four menu tabs are reached
with `I`/`J`/`C`/`M` on the keyboard and gamepad Y (`Action::Hub`), with
`TabNext`/`TabPrev` on `]`/`[` and the shoulder bumpers; `SubTabNext`/`SubTabPrev`
are `.`/`,` and the analog triggers (shared with zoom); Quit is `Q`;
ZoomIn/ZoomOut are `=`/`-` plus gamepad R2/L2; QuickSave/QuickLoad are F5/F9.
The GLFW backend only translates GLFW tokens
to `PhysicalInput` (`engine/src/platform/glfw/input_translator.cpp`) and holds
no bindings of its own.

Zoom is bound to the analog triggers (L2/R2), not the shoulder bumpers —
GLFW's mapped gamepad API reports triggers as axes
(`GLFW_GAMEPAD_AXIS_LEFT_TRIGGER`/`RIGHT_TRIGGER`, resting at -1.0 and
reading +1.0 fully pressed), not buttons, so they're handled alongside
the stick/d-pad axis logic in `poll_gamepad()` rather than in
`k_gamepad_button_bindings`.

`mouse_click_pressed` is **not** in a binding table — it's raised
directly in `translate_mouse_button()` (`input_translator.cpp`) whenever
the left button transitions to pressed, independent of the `Select`
binding above (both fire from the same physical click, deliberately).
`scroll_delta_y` similarly bypasses the binding tables — it's raised in
`translate_scroll()` from GLFW's scroll callback, since a continuous
signed magnitude has no discrete `Action` to bind to.

`poll_gamepad()` uses GLFW's _mapped_ gamepad API
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

### Resolving multiple sources per action

Several sources routinely map to one `Action` (W and Up both mean
`MoveUp`; keyboard Enter, mouse-left and gamepad A all mean `Select`),
and the stick and d-pad are independent sources for the same move
actions. `corundum::input::ActionResolver`
(`engine/include/corundum/input/action_resolver.hpp`) tracks which
sources are active per action and derives `held`/`pressed` from that
set: an action's `held` bit stays set until _every_ source bound to it
is released, and a press edge fires only on the transition from no
source active to at least one. This is what lets keyboard and gamepad
share an action without one device's release clearing the other's
input. The GLFW backend gives each binding a distinct source index
(`input_translator.cpp`) and releases the gamepad-owned sources when the
device disconnects, so a held bit cannot latch.

### Consumption sites (where each signal drives behavior)

| Signal                                        | Consumed at                                                               | Behavior                                                                                                                                             |
| --------------------------------------------- | ------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Action::MoveUp/Down/Left/Right`              | `physics_system.cpp::apply_input()`                                       | World movement                                                                                                                                       |
| `Action::MoveUp/Down/Left/Right`              | `physics_system.cpp::update_player()`                                     | Cancels an active click-to-move path                                                                                                                 |
| `Action::MoveUp/Down`                         | `dialogue/system.cpp::system()` (`NodeType::Choice`)                      | Choice cursor navigation                                                                                                                             |
| `mouse_click_pressed`                         | `physics_system.cpp::update_player()`                                     | Queues a click-to-move path via `find_path()`                                                                                                        |
| `Action::Select`                              | `dialogue_system.cpp::try_interact()`                                     | Starts dialogue (proximity-only for keyboard/gamepad; proximity **and** click-aimed-at-NPC-tile for a click — see `mouse_click_pressed` check there) |
| `Action::Select`                              | `dialogue/system.cpp::system()` (`NodeType::Talk`)                        | Advances the line                                                                                                                                    |
| `Action::Select`                              | `dialogue/system.cpp::system()` (`NodeType::Choice`)                      | Confirms the highlighted choice                                                                                                                      |
| `Action::Cancel`                              | `dialogue/conversation.cpp::Conversation::update()` (`Talk` and `Choice`) | Hard-closes dialogue (`reset()`)                                                                                                                     |
| `Action::Inventory`                           | `engine.cpp::update_engine_screens()`                                     | Opens/switches/closes the Inventory hub tab; `update_inventory()` navigates and closes it                                                             |
| `Action::Journal`                             | `engine.cpp::update_engine_screens()`                                     | Opens/switches/closes the Journal hub tab; `update_journal()` navigates and closes it                                                                 |
| `Action::Hub`                                 | `engine.cpp::update_engine_screens()`                                     | Toggles the menu hub on `Scene::last_hub_mode`                                                                                                         |
| `Action::Codex`                               | `engine.cpp::update_engine_screens()`                                     | Opens/switches/closes the Codex hub tab; `update_codex()` navigates and closes it                                                                     |
| `Action::Map`                                 | `engine.cpp::update_engine_screens()`                                     | Opens/switches/closes the Map hub tab; `update_map()` navigates and fast-travels on Activate                                                          |
| `Action::TabNext/TabPrev`                     | `engine.cpp::update_engine_screens()`                                     | Cycles the four hub tabs when one is on top; settings pages and barter Buy/Sell keep their own handling                                                 |
| `Action::Quit`                                | `engine.cpp`'s main loop                                                  | Sets `engine.quit` and closes the window                                                                                                             |
| `PlatformEvents::quit_requested`              | `engine.cpp`'s main loop                                                  | Sets `engine.quit` from an OS window-close request                                                                                                   |
| `PlatformEvents::focus_lost` / `focus_gained` | `engine.cpp`'s main loop                                                  | Pauses the simulation and audio on focus loss, resumes on focus gain                                                                                 |
| `scroll_delta_y`                              | `world/update.cpp::update_zoom()`                                         | `Camera::apply_zoom()`, anchored on the mouse cursor                                                                                                 |
| `Action::ZoomIn/ZoomOut` (held)               | `world/update.cpp::update_zoom()`                                         | `Camera::apply_zoom()`, anchored on the screen center, rate-limited by `k_zoom_rate_per_sec` and `dt`                                                |

`Action::Quit`'s keyboard source (the `Q` key) raises the action through
the `ActionResolver` like any other binding. The OS window-close button is
**not** an `Action`: the backend reports it as
`PlatformEvents::quit_requested`, so an OS lifecycle event and a player
intent never collapse into one signal. (A _different_, non-input-triggered
exit path exists for unrecoverable map-load failures in `transition.cpp`,
unrelated to any of the above.)

The main loop also consumes `PlatformEvents::focus_lost` and
`focus_gained`, pausing the simulation and audio while the window is
unfocused and clearing the loop timer's accumulator on resume so the
paused interval never replays as catch-up fixed steps.

No manual camera pan input exists yet — that isn't a gap in this doc, it's a
gap in the game. Camera zoom (above) is the one exception: the camera itself
is still a pure follow-cam with no player-controlled panning.
