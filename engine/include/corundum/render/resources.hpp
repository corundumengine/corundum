// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/render/render_state.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace corundum::core {
  class ContentSource;
}

namespace corundum::platform {
  class Renderer;
}

namespace corundum::sprites {
  class CharacterRegistry;
}

/// Process-lifetime render resources: the sprite-frame index, font families, the UI border
/// skin and the panel style. Loaded once at startup; their textures are never released.
namespace corundum::render {

  /** @brief Build the sprite-index lookup tables from the character registry.
   *  @param[in,out] r         Renderer for texture loading.
   *  @param[out]    state     Render state to populate.
   *  @param[in]     registry  Loaded character registry.
   *  @post state.sprite_index is populated and ready for render_entities().
   */
  void load_sprite_index(corundum::platform::Renderer &r, render::RenderState &state,
                         const corundum::sprites::CharacterRegistry &registry);

  /** @brief Load every role's font family into the renderer and store the resolved ids.
   *  @param[in,out] r      Renderer for font atlas creation.
   *  @param[out]    state  Render state; state.fonts is populated on success.
   *  @param[in]     paths  Resource paths supplying font_dir and the per-role file names.
   *  @return ok on success, or an error string if a required regular face fails to load.
   *  @post Each FontFamily's optional styles fall back to the closest present face; the
   *        first font id the renderer hands out (1) is reserved, so 0 always means unloaded.
   */
  [[nodiscard]] std::expected<void, std::string> load_fonts(corundum::platform::Renderer &r, render::RenderState &state,
                                                            const corundum::core::ResourcePaths &paths);

  /** @brief Load shared UI textures (dialog box border, etc.).
   *  @param[in,out] r     Renderer for texture creation.
   *  @param[out]    state Render state; panel_skin border is configured.
   *  @param[in]     path  Borders manifest to read; the border cell size is taken from the
   *                       upper_left frame, so multi-frame cells are supported.
   *  @return ok on success, or an error string on failure.
   */
  [[nodiscard]] std::expected<void, std::string>
  load_ui_assets(const corundum::core::ContentSource &content, corundum::platform::Renderer &r,
                 render::RenderState &state, std::string_view path = "data/sprite_sheets/ui/borders.json");

  /** @brief Apply panel style (colours, fonts, spacing) from game config.
   *  @param[out] state  Render state; panel_skin style is configured.
   *  @param[in]  cfg    Game config with dialogue_render settings.
   */
  void configure_panel_style(render::RenderState &state, const corundum::core::GameConfig &cfg);

} // namespace corundum::render
