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

// Process-lifetime render resources: the sprite-frame index, font families, the UI border
// skin and the panel style. Loaded once at startup; their textures are never released.
namespace corundum::render {

/** Build the sprite-index lookup tables from the character registry.
 *  @param renderer  Renderer for texture loading.
 *  @param state     Render state to populate.
 *  @param registry  Loaded character registry.
 *  @post state.sprite_index is populated and ready for render_entities().
 */
void load_sprite_index(platform::Renderer &renderer, RenderState &state, const sprites::CharacterRegistry &registry);

/** Load every role's font family into the renderer and store the resolved ids.
 *  @param renderer Renderer for font atlas creation.
 *  @param state    Render state; state.fonts is populated on success.
 *  @param paths    Resource paths supplying font_dir and the per-role file names.
 *  @return ok on success, or an error string if a required regular face fails to load.
 *  @post Each FontFamily's optional styles fall back to the closest present face; the
 *        first font id the renderer hands out (1) is reserved, so 0 always means unloaded.
 */
[[nodiscard]] std::expected<void, std::string> load_fonts(platform::Renderer &renderer, RenderState &state,
                                                          const core::ResourcePaths &paths);

/** Load shared UI textures (dialog box border, etc.).
 *  @param renderer Renderer for texture creation.
 *  @param state    Render state; panel_skin border is configured.
 *  @param paths    Supplies ui_borders_file, the borders manifest; the border cell size is taken from
 *                  its upper_left frame, so multi-frame cells are supported.
 *  @return ok on success, or an error string on failure.
 */
[[nodiscard]] std::expected<void, std::string> load_ui_assets(const core::ContentSource &content,
                                                              platform::Renderer &renderer, RenderState &state,
                                                              const core::ResourcePaths &paths);

/** Apply panel style (colours, fonts, spacing) from game config.
 *  @param state  Render state; panel_skin style is configured.
 *  @param config Game config with dialogue_render settings.
 */
void configure_panel_style(RenderState &state, const core::GameConfig &config);

} // namespace corundum::render
