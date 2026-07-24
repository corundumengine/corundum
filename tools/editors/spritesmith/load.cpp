#include "load.hpp"
#include <corundum/resources/atlas_clips.hpp>
#include <corundum/resources/atlas_clips_loader.hpp>
#include <corundum/resources/character_sheet_loader.hpp>
#include <corundum/resources/sprite_atlas.hpp>
#include <corundum/resources/sprite_sheet_clips_loader.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>

namespace tools::sprite {

  namespace {

    SheetMode detect_mode(const nlohmann::json &j) {
      if (j.contains("sprites") && j.at("sprites").is_array() && j.contains("schema_version"))
        return SheetMode::Atlas;
      if (j.contains("frames") && j.at("frames").is_object())
        return SheetMode::Character;
      return SheetMode::SpriteSheet;
    }

    void load_character(EditorState &state, const corundum::resources::CharacterSheetData &data) {
      using namespace corundum::resources;
      state.mode = SheetMode::Character;
      state.sheet_id = data.id;
      state.image_path = data.path;
      state.frame_width = data.frame_width;
      state.frame_height = data.frame_height;
      state.offset_x = data.offset_x;
      state.offset_y = data.offset_y;
      state.spacing_x = data.spacing_x;
      state.spacing_y = data.spacing_y;

      state.sprites.clear();
      state.sprites.reserve(data.sprites.size());
      for (const auto &entry : data.sprites) {
        SpriteDefinition sp;
        sp.name = entry.name;
        sp.col_span = entry.col_span;
        sp.row_span = entry.row_span;
        sp.collision_w = entry.collision_w;
        sp.collision_h = entry.collision_h;
        sp.walk_around_offset = entry.walk_around_offset;
        sp.fps = entry.fps;
        sp.anim_frames = entry.anim_frames;
        state.sprites.push_back(std::move(sp));
      }
    }

    void load_sprite_sheet_mode(EditorState &state, const std::filesystem::path &path) {
      auto result = corundum::resources::load_sprite_sheet_clips(path);
      if (!result)
        throw SheetLoadError(result.error());
      const auto &data = *result;
      state.mode = SheetMode::SpriteSheet;
      state.sheet_id = data.id;
      state.image_path = data.path;
      state.frame_width = data.frame_width;
      state.frame_height = data.frame_height;
      state.columns = data.columns;
      state.rows = data.rows;
      state.offset_x = data.offset_x;
      state.offset_y = data.offset_y;
      state.spacing_x = data.spacing_x;
      state.spacing_y = data.spacing_y;
      state.anim_fps = data.anim_fps;
      state.anim_clips.clear();
      for (const auto &clip : data.clips)
        state.anim_clips.push_back({clip.name, clip.frames});
    }

    void load_atlas_mode(EditorState &state, const std::filesystem::path &path) {
      auto result = corundum::resources::load_sprite_atlas(path);
      if (!result)
        throw SheetLoadError(result.error());
      const auto &atlas = *result;

      state.mode = SheetMode::Atlas;
      state.image_path = atlas.path;
      state.image_pixel_w = atlas.width;
      state.image_pixel_h = atlas.height;

      state.atlas_sprites = atlas.sprites;
      state.atlas_name_to_index.clear();
      for (int i = 0; i < static_cast<int>(state.atlas_sprites.size()); ++i)
        state.atlas_name_to_index[state.atlas_sprites[static_cast<std::size_t>(i)].name] = i;

      state.atlas_clips.clear();
      const auto sidecar_path = corundum::resources::atlas_clips_sidecar_path(path);
      if (std::filesystem::exists(sidecar_path)) {
        auto clips_result = corundum::resources::load_atlas_clips(sidecar_path);
        if (!clips_result)
          throw SheetLoadError(clips_result.error());
        for (const auto &clip : clips_result->clips) {
          for (const auto &frame_name : clip.frames) {
            if (!state.atlas_name_to_index.contains(frame_name))
              std::println(stderr, "[Spritesmith] Warning: clip '{}' references unknown sprite '{}' (dangling)",
                           clip.name, frame_name);
          }
          state.atlas_clips.push_back({clip.name, clip.fps, clip.frames});
        }
      }

      state.atlas_undo.clear();
      state.atlas_undo.push({state.atlas_clips, state.selected_atlas_clip});
    }

  } // namespace

  void load_sheet(EditorState &state, const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f.is_open())
      throw std::runtime_error("Cannot open: " + path.string());
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(f, nullptr, true, true);
    } catch (const nlohmann::json::parse_error &e) {
      throw SheetLoadError(std::string("JSON parse error: ") + e.what());
    }
    state.json_path = path;
    const SheetMode mode = detect_mode(j);
    if (mode == SheetMode::Atlas) {
      load_atlas_mode(state, path);
    } else if (mode == SheetMode::Character) {
      auto result = corundum::resources::load_character_sheet(path);
      if (!result)
        throw SheetLoadError(result.error());
      load_character(state, *result);
    } else {
      load_sprite_sheet_mode(state, path);
    }
  }

} // namespace tools::sprite
