#include "save.hpp"
#include <corundum/core/json_io.hpp>
#include <corundum/sprites/atlas_clips.hpp>
#include <corundum/sprites/atlas_clips_serializer.hpp>
#include <corundum/sprites/character_sheet_serializer.hpp>
#include <corundum/sprites/sprite_sheet_clips_serializer.hpp>
#include <nlohmann/json.hpp>

namespace tools::spritesmith {

  namespace {

    [[nodiscard]] corundum::sprites::CharacterSheetData to_character_sheet(const EditorState &state) {
      using namespace corundum::sprites;
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

    [[nodiscard]] corundum::sprites::SpriteSheetClips to_sprite_sheet_clips(const EditorState &state) {
      using namespace corundum::sprites;
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

    [[nodiscard]] corundum::sprites::AtlasClipsData to_atlas_clips_data(const EditorState &state) {
      corundum::sprites::AtlasClipsData data;
      for (const auto &clip : state.atlas_clips)
        data.clips.push_back({clip.name, clip.fps, clip.frames});
      return data;
    }

  } // namespace

  std::expected<void, std::string> save_sheet(EditorState &state) {
    if (state.json_path.empty())
      return std::unexpected("No save path set — enter a path in the Properties panel.");

    // Atlas mode: the atlas JSON is a read-only spritepacker artifact — only the authored
    // clips sidecar is ever written.
    if (state.mode == SheetMode::Atlas) {
      const auto sidecar_path = corundum::sprites::atlas_clips_sidecar_path(state.json_path);
      const nlohmann::json j = corundum::sprites::serialize_atlas_clips(to_atlas_clips_data(state));
      auto res = corundum::core::write_json(sidecar_path, j);
      if (!res)
        return std::unexpected(res.error());
      state.dirty = false;
      return {};
    }

    const nlohmann::json j = (state.mode == SheetMode::Character)
                                 ? corundum::sprites::serialize_character_sheet(to_character_sheet(state))
                                 : corundum::sprites::serialize_sprite_sheet_clips(to_sprite_sheet_clips(state));
    {
      auto res = corundum::core::write_json(state.json_path, j);
      if (!res)
        return std::unexpected(res.error());
    }

    state.dirty = false;
    return {};
  }

} // namespace tools::spritesmith
