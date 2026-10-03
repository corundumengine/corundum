// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/location/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  /** @brief One discovered fast-travel destination row. */
  struct MapEntry {
    std::string id{};

    bool current{false}; ///< The location's zone matches the active zone; travel is a no-op.

    std::string name{};
  };

  /** @brief Every discovered location, ordered by name.
   *
   *  A location is discovered when its `location.<id>.discovered` flag is set. `current` is
   *  set when the location's zone equals @p zone_id, so the map can mark where the player is.
   *  The row count is what the map cursor wraps against.
   */
  [[nodiscard]] std::vector<MapEntry> build_map_entries(const location::Registry &registry,
                                                        const world::FlagStore &flags, std::string_view zone_id = {});

  /** @brief Map-screen state: the highlighted row only. */
  struct MapState {
    int cursor{};
  };

  /** @brief Draw the world map as a centered list of discovered fast-travel destinations.
   *
   *  Pure render, like journal_panel_render. An empty map (nothing discovered) renders a single
   *  "(no locations)" line. The cursor is clamped into range locally; a current location is
   *  tagged "(here)".
   *
   *  @param r           Platform renderer; receives DrawRect, nine-patch DrawSprite, and DrawText commands.
   *  @param style       Dialog text style; reused so the map matches dialogue.
   *  @param border      Pre-loaded nine-patch frame; the same one the dialogue box uses.
   *  @param entries     Discovered destinations, as build_map_entries() produces.
   *  @param cursor      Highlighted row index into @p entries; clamped into range locally.
   *  @param viewport    Screen size in pixels; the panel is centered within this.
   *  @param last_device Device of the player's most recent press; picks the footer glyphs.
   */
  void map_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                        const std::vector<MapEntry> &entries, int cursor, core::math::Vec2 viewport,
                        input::InputDevice last_device = input::InputDevice::Keyboard);

} // namespace corundum::ui
