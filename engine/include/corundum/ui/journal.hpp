// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  /** @brief One selectable journal row: a started quest's name, lifecycle, and current objective. */
  struct JournalEntry {
    std::string name{};
    std::string objective{};
    gameplay::quest::Lifecycle lifecycle{gameplay::quest::Lifecycle::NotStarted};
  };

  /** @brief Build the journal rows for every started quest.
   *
   *  Rows are ordered by lifecycle section (Active, Completed, Failed) and then by quest
   *  name. `objective` is the current stage's first not-yet-done objective, falling back to
   *  its first objective, or empty when the stage has none. Pure derivation over the
   *  registry and flags; the row count is what update_journal() wraps its cursor against.
   *
   *  @param quests  Loaded quest registry.
   *  @param flags   Active FlagStore (`quest.<id>` stage keys plus objective conditions).
   *  @param zone_id Current zone; `local.<key>` objective conditions resolve against it.
   *  @return One entry per started quest, empty when none are started.
   */
  [[nodiscard]] std::vector<JournalEntry> build_journal_entries(const gameplay::quest::Registry &quests,
                                                                const world::FlagStore &flags,
                                                                std::string_view zone_id = {});

  /** @brief Draw a centered journal panel listing the started-quest rows, grouped by lifecycle.
   *
   *  Pure render — like inventory_panel_render. Each lifecycle group is headed by its name;
   *  every quest is a draw_option row (so @p cursor, clamped into range, picks the highlighted
   *  one) with its current objective drawn dimly beneath it. An empty journal renders a single
   *  "(no quests)" line. The caller only invokes this while GameMode::Journal is active.
   *
   *  @param r        Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style    Dialog text style (font id/sizes/colours); reused so the journal matches dialogue.
   *  @param border   Pre-loaded nine-patch border texture/tile dims; same one used by the dialogue box.
   *  @param entries  Journal rows to display, as build_journal_entries() produces.
   *  @param cursor   Highlighted row index into @p entries; clamped into range locally.
   *  @param viewport Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void journal_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                            const std::vector<JournalEntry> &entries, int cursor, core::math::Vec2 viewport,
                            input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
