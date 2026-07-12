#include "save.hpp"
#include <corundum/core/json_io.hpp>
#include <corundum/resources/character_sheet_serializer.hpp>
#include <corundum/resources/sprite_sheet_clips_serializer.hpp>
#include <nlohmann/json.hpp>

namespace tools::sprite {

  namespace {

    [[nodiscard]] corundum::resources::CharacterSheetData to_character_sheet(const EditorState &state) {
      using namespace corundum::resources;
      CharacterSheetData data;
      data.id = state.sheet_id;
      data.path = state.image_path;
      data.frame_width = state.frame_width;
      data.frame_height = state.frame_height;
      data.offset_x = state.offset_x;
      data.offset_y = state.offset_y;
      data.spacing_x = state.spacing_x;
      data.spacing_y = state.spacing_y;
      for (const auto &sp : state.sprites) {
        CharacterSpriteEntry entry;
        entry.name = sp.name;
        entry.col_span = sp.col_span;
        entry.row_span = sp.row_span;
        entry.collision_w = sp.collision_w;
        entry.collision_h = sp.collision_h;
        entry.walk_around_offset = sp.walk_around_offset;
        entry.fps = sp.fps;
        entry.anim_frames = sp.anim_frames;
        data.sprites.push_back(std::move(entry));
      }
      return data;
    }

    [[nodiscard]] corundum::resources::SpriteSheetClips to_sprite_sheet_clips(const EditorState &state) {
      using namespace corundum::resources;
      SpriteSheetClips data;
      data.id = state.sheet_id;
      data.path = state.image_path;
      data.columns = state.columns;
      data.rows = state.rows;
      data.frame_width = state.frame_width;
      data.frame_height = state.frame_height;
      data.offset_x = state.offset_x;
      data.offset_y = state.offset_y;
      data.spacing_x = state.spacing_x;
      data.spacing_y = state.spacing_y;
      data.anim_fps = state.anim_fps;
      for (const auto &clip : state.anim_clips)
        data.clips.push_back({clip.name, clip.frames});
      return data;
    }

  } // namespace

  std::expected<void, std::string> save_sheet(EditorState &state) {
    if (state.json_path.empty())
      return std::unexpected("No save path set — enter a path in the Properties panel.");

    const nlohmann::json j = (state.mode == SheetMode::Character)
                                 ? corundum::resources::serialize_character_sheet(to_character_sheet(state))
                                 : corundum::resources::serialize_sprite_sheet_clips(to_sprite_sheet_clips(state));
    {
      auto res = corundum::core::write_json(state.json_path, j);
      if (!res)
        return std::unexpected(res.error());
    }

    state.dirty = false;
    return {};
  }

} // namespace tools::sprite
