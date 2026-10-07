// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/save/save.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  /** @brief The slot list a Save/Load screen shows.
   *
   *  Extends save::list_saves() so the list always offers every manual slot, even one whose
   *  file does not exist yet: load mode shows it as empty, save mode lets the player write it.
   *  Autosave and quicksave appear only when they exist, and a corrupt slot keeps its @c error.
   *
   *  @param directory Saves directory (see save::saves_directory).
   *  @param game_id   Current game's id; slots written for another game become error rows.
   */
  [[nodiscard]] std::vector<save::SaveSlotInfo> build_slot_rows(const std::filesystem::path &directory,
                                                                std::string_view game_id);

  /** @brief The slot id of the newest valid save, or empty when none is valid.
   *
   *  "Newest" is the greatest SaveMeta::saved_at_unix among rows that have metadata; autosave
   *  and quicksave count. Corrupt and empty rows are ignored. */
  [[nodiscard]] std::optional<std::string> newest_valid_slot(const std::vector<save::SaveSlotInfo> &slots);

  /** @brief A slot's display label: the manual slot number, or a named autosave/quicksave. */
  [[nodiscard]] std::string slot_display_name(std::string_view slot_id);

  /** @brief True when @p info is a slot with no file (selectable only for saving). */
  [[nodiscard]] bool slot_is_empty(const save::SaveSlotInfo &info) noexcept;

  /** @brief Save/Load screen state: mode, highlighted row, scroll offset and the slot rows. */
  struct SaveLoadState {
    bool saving{}; ///< True in save mode, false in load mode.

    int cursor{};

    int scroll{};

    std::vector<save::SaveSlotInfo> slots{};
  };

  /** @brief Screen-space geometry of the Save/Load panel, shared by render and hit-testing. */
  struct SaveLoadLayout {
    core::math::Vec2 panel_pos{};

    core::math::Vec2 panel_size{};

    ui::ListHit rows{}; ///< Left-hand slot list.

    ui::RowRect detail{}; ///< Right-hand detail pane.
  };

  /** @brief Compute the Save/Load panel's geometry for @p viewport, scrolling to keep the cursor
   *  visible. */
  [[nodiscard]] SaveLoadLayout save_load_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                                      const SaveLoadState &state, core::math::Vec2 viewport,
                                                      input::InputDevice last_device);

  /** @brief Draw the Save/Load screen: a viewport-filling panel with the slot list on the left and
   *  the selected slot's detail on the right.
   *
   *  Pure render. Empty slots draw as "(empty)"; corrupt slots show their error string in the
   *  detail pane and cannot be loaded.
   */
  void save_load_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const SaveLoadState &state, core::math::Vec2 viewport,
                              input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::gameplay::screens
