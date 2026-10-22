// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/render/resources.hpp>

#include <corundum/core/content_source.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/ui/font_family.hpp>

#include "core/warn_log.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using corundum::core::math::IntRect;
using corundum::sprites::k_null_sprite_id;
using corundum::sprites::k_num_anim_ids;
using corundum::sprites::rendered_frame_height;
using corundum::sprites::rendered_frame_width;
using corundum::sprites::SpriteId;

namespace corundum::render {

  namespace {

    [[nodiscard]] SpriteId max_sprite_id(const corundum::sprites::CharacterRegistry &registry) noexcept {
      SpriteId max_id = 0;
      for (const auto &[name, frame] : registry.frames())
        max_id = std::max(max_id, frame.sprite_id);
      return max_id;
    }

    /// Process-lifetime textures: references stored in sprite_index are deliberately never
    /// released, so area purges can never destroy them.
    [[nodiscard]] std::unordered_map<corundum::sprites::Id, uint32_t>
    load_sheet_textures(corundum::platform::Renderer &r, const corundum::sprites::CharacterRegistry &registry) {
      std::unordered_map<corundum::sprites::Id, uint32_t> sheet_textures;
      for (const auto &[sheet_id, sheet] : registry.sheets()) {
        auto result = r.load_texture(sheet.path);
        if (result.has_value())
          sheet_textures[sheet_id] = result.value();
        else
          corundum::detail::warn_log("[renderer] WARNING: could not load texture '{}'", sheet.path);
      }
      return sheet_textures;
    }

    void store_sprite_frames(SpriteFrameIndex &index, const corundum::sprites::Frames &frame,
                             const corundum::sprites::SpriteSheet &sheet, uint32_t texture_id) {
      index.tex_by_sprite_id[frame.sprite_id] = texture_id;
      const auto sprite_id = static_cast<std::size_t>(frame.sprite_id);
      if (sprite_id >= index.walk_offsets.size())
        index.walk_offsets.resize(sprite_id + 1, 0.f);
      index.walk_offsets[sprite_id] = frame.walk_around_offset;

      const int frame_width = rendered_frame_width(frame.col_span, sheet.frame_width, sheet.spacing_x);
      const int frame_height = rendered_frame_height(frame.row_span, sheet.frame_height, sheet.spacing_y);
      for (uint8_t anim = 0; anim < k_num_anim_ids; ++anim) {
        const auto &coords = frame.anim_frames[anim];
        const std::size_t slot = (static_cast<std::size_t>(frame.sprite_id) * k_num_anim_ids) + anim;
        index.anim_offsets[slot] = static_cast<uint32_t>(index.frame_rects.size());
        index.anim_frame_counts[slot] = static_cast<uint8_t>(coords.size());
        for (const auto &coord : coords) {
          const sprites::IntPoint origin = frame_origin(sheet, coord);
          index.frame_rects.push_back(
              IntRect{.x = origin.x, .y = origin.y, .width = frame_width, .height = frame_height});
        }
      }
    }

  } // namespace

  void load_sprite_index(corundum::platform::Renderer &r, render::RenderState &state,
                         const corundum::sprites::CharacterRegistry &registry) {
    const std::size_t num_sprites = static_cast<std::size_t>(max_sprite_id(registry)) + 1;
    state.sprite_index.tex_by_sprite_id.assign(num_sprites, std::nullopt);
    const std::size_t num_slots = num_sprites * k_num_anim_ids;
    state.sprite_index.anim_offsets.assign(num_slots, 0);
    state.sprite_index.anim_frame_counts.assign(num_slots, 0);
    state.sprite_index.frame_rects.clear();

    const std::unordered_map<corundum::sprites::Id, uint32_t> sheet_textures = load_sheet_textures(r, registry);
    for (const auto &[name, frame] : registry.frames()) {
      if (frame.sprite_id == k_null_sprite_id)
        continue;
      const corundum::sprites::SpriteSheet *sheet = registry.get_sheet(frame.sheet_id);
      if (sheet == nullptr)
        continue;
      const auto texture = sheet_textures.find(frame.sheet_id);
      if (texture == sheet_textures.end())
        continue;

      store_sprite_frames(state.sprite_index, frame, *sheet, texture->second);
    }
  }

  namespace {

    /// Loads one family's regular face and resolves every optional style to the closest present
    /// face. An absent or unloadable optional face logs a warning and falls back; only a failed
    /// regular face is an error.
    std::expected<void, std::string> load_font_family(corundum::platform::Renderer &r, const std::string &font_dir,
                                                      std::string_view role_name,
                                                      const corundum::ui::FontFamilyPaths &family_paths,
                                                      corundum::ui::FontFamily &family) {
      const auto load_face = [&](const std::string &name) -> std::expected<std::uint32_t, std::string> {
        return r.load_font(std::format("{}/{}", font_dir, name));
      };

      auto regular = load_face(family_paths.regular);
      if (!regular)
        return std::unexpected(std::format("[renderer] could not load {} font '{}': {}", role_name,
                                           family_paths.regular, regular.error()));

      const auto set_style = [&](corundum::ui::FontStyle style, std::uint32_t id) {
        family.ids[static_cast<std::size_t>(style)] = id;
      };
      const auto regular_id = *regular;
      set_style(corundum::ui::FontStyle::Regular, regular_id);

      // An optional face returns its id, or 0 when the config omits it or its file fails to
      // load; the caller then substitutes the closest present style.
      const auto load_optional = [&](const std::string &name) -> std::uint32_t {
        if (name.empty())
          return 0;
        auto loaded = load_face(name);
        if (loaded)
          return *loaded;
        corundum::detail::warn_log("[renderer] WARNING: could not load {} font '{}': {}; using fallback", role_name,
                                   name, loaded.error());
        return 0;
      };

      const std::uint32_t bold_id = load_optional(family_paths.bold);
      set_style(corundum::ui::FontStyle::Bold, bold_id != 0 ? bold_id : regular_id);

      const std::uint32_t italic_id = load_optional(family_paths.italic);
      set_style(corundum::ui::FontStyle::Italic, italic_id != 0 ? italic_id : regular_id);

      const std::uint32_t bold_italic_fallback = bold_id != 0 ? bold_id : italic_id;
      const std::uint32_t bold_italic_id = load_optional(family_paths.bold_italic);
      std::uint32_t resolved_bold_italic = bold_italic_id;
      if (resolved_bold_italic == 0)
        resolved_bold_italic = bold_italic_fallback != 0 ? bold_italic_fallback : regular_id;
      set_style(corundum::ui::FontStyle::BoldItalic, resolved_bold_italic);
      return {};
    }

  } // namespace

  std::expected<void, std::string> load_fonts(corundum::platform::Renderer &r, render::RenderState &state,
                                              const corundum::core::ResourcePaths &paths) {
    const auto ui_slot = static_cast<std::size_t>(corundum::ui::FontRole::Ui);
    const auto display_slot = static_cast<std::size_t>(corundum::ui::FontRole::Display);

    constexpr std::array k_roles{
        std::pair{corundum::ui::FontRole::Dialogue, std::string_view{"dialogue"}},
        std::pair{corundum::ui::FontRole::Quest, std::string_view{"quest"}},
        std::pair{corundum::ui::FontRole::Ui, std::string_view{"ui"}},
    };

    for (const auto &[role, role_name] : k_roles) {
      const auto slot = static_cast<std::size_t>(role);
      auto res = load_font_family(r, paths.font_dir, role_name, paths.fonts[slot], state.fonts[slot]);
      if (!res)
        return res;
    }

    // The display family is optional; without one, its text draws in the ui family.
    if (paths.fonts[display_slot].regular.empty()) {
      state.fonts[display_slot] = state.fonts[ui_slot];
      return {};
    }
    return load_font_family(r, paths.font_dir, "display", paths.fonts[display_slot], state.fonts[display_slot]);
  }

  std::expected<void, std::string> load_ui_assets(const corundum::core::ContentSource &content,
                                                  corundum::platform::Renderer &r, render::RenderState &state,
                                                  std::string_view path) {
    auto j_result = corundum::core::read_json(content, path);
    if (!j_result) {
      corundum::detail::warn_log("[renderer] WARNING: could not load '{}': {}", path, j_result.error());
      return {};
    }
    const nlohmann::json &j = *j_result;

    int fw = 0;
    int fh = 0;
    std::string tex_path;
    try {
      fw = j.at("frame_width").get<int>();
      fh = j.at("frame_height").get<int>();
      tex_path = j.at("path").get<std::string>();
    } catch (const nlohmann::json::exception &e) {
      return std::unexpected(std::format("[renderer] malformed UI assets '{}': {}", path, e.what()));
    }

    const auto make_rect = [&](std::string_view name) -> IntRect {
      const auto &fr = j.at("frames").at(std::string{name});
      const int col = fr.at("col").get<int>();
      const int row = fr.at("row").get<int>();
      const int cs = fr.at("col_span").get<int>();
      const int rs = fr.at("row_span").get<int>();
      return IntRect{.x = col * fw, .y = row * fh, .width = cs * fw, .height = rs * fh};
    };

    // Process-lifetime texture: the UI border reference is deliberately never released, so
    // area purges can never destroy it.
    auto result = r.load_texture(tex_path);
    if (result.has_value()) {
      state.panel_skin.border.texture_id = result.value();
    } else {
      corundum::detail::warn_log("[renderer] WARNING: could not load texture '{}'", tex_path);
      state.panel_skin.border.texture_id = 0;
    }

    // NinePatchBorder lays the frame out as a uniform 3×3 grid. A cell is the size of the
    // upper_left frame — one frame in some atlases, but multi-frame in others (the shipped
    // border uses 2×2-frame 32px cells), so the span must come from the frame, not frame_width.
    IntRect ul{};
    try {
      ul = make_rect("upper_left");
    } catch (const nlohmann::json::exception &e) {
      return std::unexpected(std::format("[renderer] malformed UI assets '{}': {}", path, e.what()));
    }
    state.panel_skin.border.tile_w = ul.width;
    state.panel_skin.border.tile_h = ul.height;

    return {};
  }

  void configure_panel_style(render::RenderState &state, const corundum::core::GameConfig &cfg) {
    const auto &dr = cfg.dialogue_render;
    const float scale = state.ui_scale;
    const auto scaled = [scale](unsigned size) {
      return static_cast<unsigned>(std::lround(static_cast<float>(size) * scale));
    };
    state.panel_skin.style = corundum::ui::PanelStyle{
        .fonts = state.fonts,
        .font_size_speaker = scaled(dr.font_size_speaker),
        .font_size_body = scaled(dr.font_size_body),
        .font_size_prompt = scaled(dr.font_size_prompt),
        .font_size_heading = scaled(cfg.display.heading_size),
        .font_size_banner = scaled(cfg.display.banner_size),
        .margin = dr.margin * scale,
        .line_spacing = dr.line_spacing * scale,
        .panel_height_frac = dr.panel_height_frac,
        .bg = {.r = 10, .g = 10, .b = 20, .a = 220},
        .speaker = {.r = 180, .g = 160, .b = 255, .a = 255},
        .body = {.r = 220, .g = 220, .b = 220, .a = 255},
        .choice = {.r = 180, .g = 180, .b = 180, .a = 255},
        .selected = {.r = 255, .g = 230, .b = 100, .a = 255},
    };
  }

} // namespace corundum::render
