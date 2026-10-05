// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief Which lifecycle section of the journal is showing. */
  enum class JournalTab : std::uint8_t {
    Active = 0,
    Completed = 1,
    Failed = 2,
  };

  /** @brief The journal sub-tabs, in strip order. */
  inline constexpr std::array<JournalTab, 3> k_journal_tabs{
      JournalTab::Active,
      JournalTab::Completed,
      JournalTab::Failed,
  };

  /** @brief Display label for journal sub-tab @p tab. */
  [[nodiscard]] std::string_view journal_tab_label(JournalTab tab) noexcept;

  /** @brief Advance @p tab by @p direction (+1 / -1) steps, wrapping. */
  [[nodiscard]] JournalTab cycle_journal_tab(JournalTab tab, int direction) noexcept;

  /** @brief One objective of a quest's current stage, with its done mark. */
  struct JournalObjective {
    std::string text{};
    bool done{false};
  };

  /** @brief One selectable journal row: a started quest's name, lifecycle, and current objective.
   *
   *  `objectives` carries the highlighted row's full stage checklist; a non-highlighted row draws
   *  only its single `objective` line. `tracked` mirrors the `quest.<id>.tracked` flag.
   */
  struct JournalEntry {
    std::string id{};
    std::string name{};
    std::string objective{};
    gameplay::quest::Lifecycle lifecycle{gameplay::quest::Lifecycle::NotStarted};
    bool tracked{false};
    std::vector<JournalObjective> objectives{};
  };

  /** @brief Journal-screen state: the active sub-tab and the highlighted row within it. */
  struct JournalState {
    JournalTab tab{JournalTab::Active};
    int cursor{};
  };

  /** @brief Build the journal rows for every started quest in @p tab.
   *
   *  Quests are filtered to the lifecycle section @p tab names and sorted by name. `objective` is
   *  the current stage's first not-yet-done objective, falling back to its first objective, or
   *  empty when the stage has none; `objectives` is the full checklist of that stage. Pure
   *  derivation over the registry and flags; the row count is what update_journal() wraps its
   *  cursor against.
   *
   *  @param quests  Loaded quest registry.
   *  @param flags   Active FlagStore (`quest.<id>` stage keys plus objective conditions).
   *  @param tab     Lifecycle section to list.
   *  @param zone_id Current zone; `local.<key>` objective conditions resolve against it.
   *  @return One entry per started quest in @p tab, empty when none qualify.
   */
  [[nodiscard]] std::vector<JournalEntry> build_journal_entries(const gameplay::quest::Registry &quests,
                                                                const world::FlagStore &flags, JournalTab tab,
                                                                std::string_view zone_id = {});

  /** @brief Screen-space geometry of the journal panel, shared by render and mouse hit-testing. */
  struct JournalLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    std::array<ui::RowRect, k_journal_tabs.size()> sub_tabs{}; ///< Active, Completed, Failed.

    std::vector<ui::RowRect> rows{}; ///< One hit rect per entry of @p entries, in draw order.
  };

  /** @brief Compute the journal panel's geometry for @p viewport. */
  [[nodiscard]] JournalLayout journal_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                                   const std::vector<JournalEntry> &entries, const JournalState &state,
                                                   core::math::Vec2 viewport, input::InputDevice last_device);

  /** @brief Draw a centered journal panel listing one lifecycle tab's rows.
   *
   *  Pure render — like inventory_panel_render. The Active/Completed/Failed sub-tab strip is
   *  drawn under the title with @p state.tab highlighted; every quest is a draw_option row (so
   *  @p state.cursor, clamped into range, picks the highlighted one) showing its current objective.
   *  The highlighted quest additionally expands to its full objective checklist, each line marked
   *  `[x]` or `[ ]` from JournalObjective::done, and a tracked quest carries a `*` mark. An empty
   *  tab renders a single "(no quests)" line. The caller only invokes this while GameMode::Journal
   *  is active.
   *
   *  @param r           Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style       Dialog text style (font, size, colour); reused so the journal matches dialogue.
   *  @param border      Pre-loaded nine-patch border texture/tile dims; same one used by the dialogue box.
   *  @param entries     Journal rows to display, as build_journal_entries() produces.
   *  @param state       Active sub-tab and highlighted row index into @p entries (clamped locally).
   *  @param viewport    Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void journal_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                            const std::vector<JournalEntry> &entries, const JournalState &state,
                            core::math::Vec2 viewport, input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
