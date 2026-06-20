#include "load.hpp"
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

    void load_character(EditorState &state, const nlohmann::json &j) {
      using namespace corundum::resources;
      state.mode = SheetMode::Character;
      try {
        state.sheet_id = j.at("id").get<std::string>();
        state.image_path = j.at("path").get<std::string>();
        state.frame_width = j.at("frame_width").get<int>();
        state.frame_height = j.at("frame_height").get<int>();
      } catch (const nlohmann::json::exception &e) {
        throw SheetLoadError(std::string("character sheet missing required field: ") + e.what());
      }
      state.offset_x = j.value("offset_x", 0);
      state.offset_y = j.value("offset_y", 0);
      state.spacing_x = j.value("spacing_x", 0);
      state.spacing_y = j.value("spacing_y", 0);

      state.sprites.clear();
      for (const auto &[name, sj] : j.at("frames").items()) {
        SpriteDefinition sp;
        sp.name = name;
        try {
          sp.col_span = sj.at("col_span").get<int>();
          sp.row_span = sj.at("row_span").get<int>();
        } catch (const nlohmann::json::exception &e) {
          throw SheetLoadError("sprite '" + name + "': " + e.what());
        }
        sp.collision_w = sj.value("collision_w", 0);
        sp.collision_h = sj.value("collision_h", 0);
        sp.walk_around_offset = sj.value("walk_around_offset", 0.6f);
        for (uint8_t i = 0; i < k_num_anim_ids; ++i) {
          const std::string aname(k_anim_names[i]);
          if (!sj.contains(aname))
            continue;
          for (const auto &fc : sj.at(aname))
            sp.anim_frames[i].push_back({fc.at("col").get<int>(), fc.at("row").get<int>()});
        }
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
    if (detect_mode(j) == SheetMode::Character)
      load_character(state, j);
    else
      load_sprite_sheet(state, j);
  }

} // namespace tools::sprite
