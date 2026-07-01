#include "load.hpp"
#include <corundum/resources/character_sheet_loader.hpp>
#include <corundum/resources/sprite.hpp>
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
        sp.anim_frames = entry.anim_frames;
        state.sprites.push_back(std::move(sp));
      }
    }

    void load_sprite_sheet(EditorState &state, const nlohmann::json &j) {
      state.mode = SheetMode::SpriteSheet;
      state.sheet_id = j.value("id", std::string{});
      try {
        state.image_path = j.at("path").get<std::string>();
        state.frame_width = j.at("tile_width").get<int>();
        state.frame_height = j.at("tile_height").get<int>();
        state.columns = j.at("columns").get<int>();
        state.rows = j.at("rows").get<int>();
      } catch (const nlohmann::json::exception &e) {
        throw SheetLoadError(std::string("sprite sheet missing required field: ") + e.what());
      }
      state.offset_x = j.value("offset_x", 0);
      state.offset_y = j.value("offset_y", 0);
      state.spacing_x = j.value("spacing_x", 0);
      state.spacing_y = j.value("spacing_y", 0);

      state.anim_clips.clear();
      state.anim_fps = 2;
      if (!j.contains("animations"))
        return;
      const auto &aj = j.at("animations");
      state.anim_fps = aj.value("fps", 2);
      for (const auto &cj : aj.at("clips")) {
        AnimClip clip;
        clip.name = cj.at("name").get<std::string>();
        for (const auto &fc : cj.at("frames"))
          clip.frames.push_back({fc.at("col").get<int>(), fc.at("row").get<int>()});
        state.anim_clips.push_back(std::move(clip));
      }
    }

  } // namespace

  void load_sheet(EditorState &state, const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f.is_open())
      throw std::runtime_error("Cannot open: " + path.string());
    nlohmann::json j;
    try {
      j = nlohmann::json::parse(f);
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
      load_sprite_sheet(state, j);
    }
  }

} // namespace tools::sprite
