#include "load.hpp"
#include <corundum/resources/character_sheet_loader.hpp>
#include <corundum/resources/sprite_sheet_clips_loader.hpp>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools::sprite {

  namespace {

    SheetMode detect_mode(const nlohmann::json &j) {
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
    if (detect_mode(j) == SheetMode::Character) {
      auto result = corundum::resources::load_character_sheet(path);
      if (!result)
        throw SheetLoadError(result.error());
      load_character(state, *result);
    } else {
      load_sprite_sheet_mode(state, path);
    }
  }

} // namespace tools::sprite
