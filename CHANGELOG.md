# Changelog

All notable development milestones for this project will be documented in
this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html);
while the major version is 0, the public API may change between minor releases.

## Unreleased

Active development in progress.

## 0.2.0 - 2026-09-30

The first milestone: a working isometric RPG engine, from the frame loop and
renderer through quests and saves, with the editors to author content for it.

- Fixed-timestep simulation with render interpolation, a deterministic RNG, and
  pause handling for focus loss and controller disconnect; engine hooks for
  game-specific behaviour.
- SoA entity system with generation-counted handles and two-phase deletion.
- Isometric renderer with Metal (macOS) and GLCore (Windows/Linux) backends,
  elevation-aware draw order and collision, viewport culling, and UTF-8 text.
- Sprite animation driven by atlas clips and character sheets.
- Single-map and chunked world modes, portal transitions, spawn points,
  chunk-resident actor streaming, and click-to-move pathfinding over walkable
  terrain, stairs and ramps.
- Dialogue, quest, item, inventory, reputation and flag systems.
- In-game UI: dialogue and prompt boxes, inventory panel, and nine-patch
  drawing, plus a debug overlay.
- Save/load with a versioned save format and migration chain, and
  QuickSave/QuickLoad input actions.
- JSON Schema validation and schema versioning across every data format.
- Runtime input bindings, gamepad hot-plug, borderless fullscreen, and per-user
  settings.
- GLFW windowing and input with a null backend for headless tests, plus
  sokol_audio behind a swappable `AudioBackend`.
- Tilesmith, Spritesmith and Loom editors, a headless `mapview` tool, and the
  shared `corundum::toolkit` editor library.
- Doxygen API documentation and authoring guides for tilemaps, dialogue,
  quests, saving and controls.
- CI builds and tests on macOS, Windows and Linux.
- Licensed under Apache-2.0.
