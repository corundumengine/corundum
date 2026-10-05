# Corundum Engine

[![C++23](https://img.shields.io/badge/c%2B%2B-23-blue.svg)](https://isocpp.org/)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![CI](https://github.com/corundumengine/corundum/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/corundumengine/corundum/actions/workflows/ci.yml)

<p align="center">
  <a href="https://corundumengine.com">
    <img src="branding/logo.png" width="500" alt="Corundum Engine logo">
  </a>
</p>

A data-oriented C++23 engine and editor toolset for building 2D isometric CRPGs on
desktop — macOS, Windows and Linux.

**Pre-alpha.** In active development by a single developer. Not ready for building
your own games: APIs, file formats and tools change without notice.

[Website](https://corundumengine.com) · [Devlog](https://corundumengine.com/blog/)

## What it is

- **Data-oriented runtime** — per-frame systems run over structure-of-arrays component
  tables; stateful logic (dialogue, quests, world streaming) lives in plain classes.
- **JSON-driven content** — dialogue graphs, quests, items, tilemaps, character sheets
  and world manifests are authored data, not code.
- **Integrated editors** — Tilesmith for tilemaps, Spritesmith for sprite sheets and
  Loom for dialogue, quests and items.

## Platforms

| Platform              | Toolchain                         | Graphics        | Status                       |
| --------------------- | --------------------------------- | --------------- | ---------------------------- |
| macOS (Apple Silicon) | LLVM, libc++                      | Metal           | Primary development platform |
| Windows 11            | LLVM clang++ (MSVC ABI), MSVC STL | OpenGL (GLCore) | Built and tested in CI       |
| Ubuntu (LLVM 21)      | LLVM clang++, libc++              | OpenGL (GLCore) | Built and tested in CI       |

CI builds and tests all three platforms on every push and pull request.

## Tools

- **Tilesmith** — tilemap editor
- **Spritesmith** — sprite sheet editor
- **Loom** — content editor for dialogue, quests and items

Guides (work in progress; formats may change):

- [Building tilemaps](docs/building-tilemaps.md)
- [Writing dialogue](docs/writing-dialogue.md)
- [Writing quests](docs/writing-quests.md)
- [Fonts](docs/fonts.md)
- [Saving](docs/saving.md)
- [Controls](docs/controls.md)

## Dependencies

| Library                                                                             | Purpose                     |
| ----------------------------------------------------------------------------------- | --------------------------- |
| [nlohmann/json](https://github.com/nlohmann/json)                                   | JSON parsing                |
| [ImGui](https://github.com/ocornut/imgui)                                           | Editor GUI                  |
| [GLFW](https://github.com/glfw/glfw)                                                | Windowing and input         |
| [sokol](https://github.com/floooh/sokol)                                            | GPU rendering, audio        |
| [stb](https://github.com/nothings/stb)                                              | Image loading, OGG decoding |
| [FreeType](https://freetype.org)                                                    | Font rasterization          |
| [doctest](https://github.com/doctest/doctest)                                       | Unit testing                |
| [nlohmann-json-schema-validator](https://github.com/pboettch/json-schema-validator) | JSON schema validation      |

## License & trademarks

Engine and tools are Apache-2.0 — see [LICENSE](LICENSE) for details.

"Corundum Engine" and its logo are trademarks of Gentle Lion Studios, Inc. — see
[TRADEMARKS.md](TRADEMARKS.md).

Dependencies carry their own licenses (see [THIRD_PARTY_NOTICES](THIRD_PARTY_NOTICES.md)).
