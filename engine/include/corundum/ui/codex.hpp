// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <vector>

namespace corundum::ui {

  /** @brief Every unlocked codex entry, ordered by (category, title).
   *
   *  Only entries whose `codex.<id>` flag is set are included. Pure derivation over
   *  the registry and flags; the row count is what the codex cursor wraps against.
   *
   *  @param registry Loaded codex registry.
   *  @param flags    Active FlagStore.
   *  @return One entry per unlocked codex entry, empty when none are unlocked.
   */
  [[nodiscard]] std::vector<gameplay::codex::CodexEntry> build_codex_entries(const gameplay::codex::Registry &registry,
                                                                             const world::FlagStore &flags);

  /** @brief Codex-screen state: the highlighted row and the cached unlocked-entry list.
   *
   *  A stateful class in the AGENTS.md sense — the cached rows outlive a single call and are
   *  invalidated only by an unlock or an explicit open, mirroring RenderState::above_z_cache.
   *  Rebuild through refresh_codex() rather than mutating @c entries directly.
   */
  struct CodexState {
    int cursor{};

    float scroll{};

    /// True when @c entries no longer reflects the registry + flags; refresh_codex() rebuilds.
    bool dirty{true};

    /// Cached unlocked entries, ordered by (category, title).
    std::vector<gameplay::codex::CodexEntry> entries{};
  };

  /** @brief Rebuild @p state's cached rows when they are stale. No-cost when not dirty. */
  void refresh_codex(CodexState &state, const gameplay::codex::Registry &registry, const world::FlagStore &flags);

  /** @brief Mark @p state's cached rows stale (call on open and on every codex unlock). */
  inline void codex_mark_dirty(CodexState &state) noexcept {
    state.dirty = true;
  }

  /** @brief Draw the codex as a centered two-pane panel: unlocked entries left, body right.
   *
   *  Pure render. An empty codex renders a single "(no entries)" line. The selected entry's
   *  body is word-wrapped into the right pane; the cursor is clamped into range locally.
   *
   *  @param r           Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style       Dialog text style; reused so the codex matches dialogue.
   *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
   *  @param entries     Unlocked rows, as refresh_codex() stores them.
   *  @param cursor      Highlighted row index into @p entries; clamped into range locally.
   *  @param scroll      First body line to draw; clamped to the wrapped body length.
   *  @param viewport    Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void codex_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                          const std::vector<gameplay::codex::CodexEntry> &entries, int cursor, float scroll,
                          core::math::Vec2 viewport, input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
