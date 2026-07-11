#include "save.hpp"
#include <corundum/core/json_io.hpp>
#include <corundum/resources/sprite.hpp>
#include <nlohmann/json.hpp>

namespace tools::sprite {

  namespace {

    nlohmann::json build_character_json(const EditorState &state) {
      using namespace corundum::resources;
      nlohmann::json j;
      j["id"] = state.sheet_id;
      j["path"] = state.image_path;
      j["frame_width"] = state.frame_width;
      j["frame_height"] = state.frame_height;
      if (state.offset_x)
        j["offset_x"] = state.offset_x;
      if (state.offset_y)
        j["offset_y"] = state.offset_y;
      if (state.spacing_x)
        j["spacing_x"] = state.spacing_x;
      if (state.spacing_y)
        j["spacing_y"] = state.spacing_y;

      j["frames"] = nlohmann::json::object();
      for (const auto &sp : state.sprites) {
        nlohmann::json sj;
        sj["col_span"] = sp.col_span;
        sj["row_span"] = sp.row_span;
        if (sp.collision_w > 0)
          sj["collision_w"] = sp.collision_w;
        if (sp.collision_h > 0)
          sj["collision_h"] = sp.collision_h;
        sj["walk_around_offset"] = sp.walk_around_offset;
        if (sp.fps > 0.f)
          sj["fps"] = sp.fps;
        for (uint8_t i = 0; i < k_num_anim_ids; ++i) {
          if (sp.anim_frames[i].empty())
            continue;
          auto &arr = sj[std::string(k_anim_names[i])] = nlohmann::json::array();
          for (const auto &fc : sp.anim_frames[i])
            arr.push_back({{"col", fc.col}, {"row", fc.row}});
        }
        j["frames"][sp.name] = sj;
      }
      return j;
    }

    nlohmann::json build_sprite_sheet_json(const EditorState &state) {
      nlohmann::json j;
      j["id"] = state.sheet_id;
      j["columns"] = state.columns;
      j["rows"] = state.rows;
      j["path"] = state.image_path;
      j["frame_width"] = state.frame_width;
      j["frame_height"] = state.frame_height;
      if (state.offset_x)
        j["offset_x"] = state.offset_x;
      if (state.offset_y)
        j["offset_y"] = state.offset_y;
      if (state.spacing_x)
        j["spacing_x"] = state.spacing_x;
      if (state.spacing_y)
        j["spacing_y"] = state.spacing_y;

      if (!state.anim_clips.empty()) {
        nlohmann::json aj;
        aj["fps"] = state.anim_fps;
        aj["clips"] = nlohmann::json::array();
        for (const auto &clip : state.anim_clips) {
          nlohmann::json cj;
          cj["name"] = clip.name;
          cj["frames"] = nlohmann::json::array();
          for (const auto &fc : clip.frames)
            cj["frames"].push_back({{"col", fc.col}, {"row", fc.row}});
          aj["clips"].push_back(cj);
        }
        j["animations"] = aj;
      }
      return j;
    }

  } // namespace

  std::expected<void, std::string> save_sheet(EditorState &state) {
    if (state.json_path.empty())
      return std::unexpected("No save path set — enter a path in the Properties panel.");

    const nlohmann::json j =
        (state.mode == SheetMode::Character) ? build_character_json(state) : build_sprite_sheet_json(state);
    {
      auto res = corundum::core::write_json(state.json_path, j);
      if (!res)
        return std::unexpected(res.error());
    }

    state.dirty = false;
    return {};
  }

} // namespace tools::sprite
