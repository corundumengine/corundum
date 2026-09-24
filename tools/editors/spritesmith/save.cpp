// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "save.hpp"
#include "editor_state.hpp"
#include <corundum/core/json_io.hpp>
#include <corundum/sprites/atlas_clips.hpp>
#include <corundum/sprites/atlas_clips_serializer.hpp>
#include <corundum/sprites/character_sheet_loader.hpp>
#include <corundum/sprites/character_sheet_serializer.hpp>
#include <corundum/sprites/sprite_sheet_clips.hpp>
#include <corundum/sprites/sprite_sheet_clips_serializer.hpp>
#include <corundum/toolkit/widgets/file_browser.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json_fwd.hpp>
#include <print>
#include <string>
#include <unordered_set>
#include <utility>

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
        entry.footprint_col_span = sp.footprint_col_span;
        entry.footprint_row_span = sp.footprint_row_span;
        // spritesmith always exposes both footprint fields, so every save authors them.
        entry.footprint_authored = true;
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
        data.clips.push_back({.name = clip.name, .frames = clip.frames});
      return data;
    }

    [[nodiscard]] corundum::sprites::AtlasClipsData to_atlas_clips_data(const EditorState &state) {
      corundum::sprites::AtlasClipsData data;
      for (const auto &clip : state.atlas_clips)
        data.clips.push_back({.fps = clip.fps, .frames = clip.frames, .name = clip.name});
      return data;
    }

    /// Reject clip names the loader refuses (empty or duplicated), so a save can never write a
    /// sheet that load_sprite_sheet_clips() would then reject.
    [[nodiscard]] std::expected<void, std::string> validate_sprite_sheet_clip_names(const EditorState &state) {
      std::unordered_set<std::string> seen;
      for (const auto &clip : state.anim_clips) {
        if (clip.name.empty())
          return std::unexpected("Animation clip names must not be empty.");
        if (!seen.insert(clip.name).second)
          return std::unexpected(std::format("Duplicate animation clip name '{}'.", clip.name));
      }
      return {};
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

    if (state.mode == SheetMode::SpriteSheet) {
      if (auto valid = validate_sprite_sheet_clip_names(state); !valid)
        return std::unexpected(valid.error());
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

  void open_save_as_browser(EditorState &state) {
    const auto start = state.json_path.empty() ? std::filesystem::current_path() : state.json_path.parent_path();
    const std::string default_name = state.json_path.empty() ? state.sheet_id : state.json_path.filename().string();
    corundum::toolkit::widgets::open_save_browser(state.save_browser, "Save Sprite Sheet As", start,
                                                  {{"Sprite Sheet JSON", {"json"}}}, default_name);
  }

  void action_save(EditorState &state) {
    if (state.json_path.empty()) {
      open_save_as_browser(state);
      return;
    }
    if (auto r = save_sheet(state); !r)
      std::println(stderr, "[Spritesmith] Save failed: {}", r.error());
    else
      std::println("[Spritesmith] Saved: {}", state.json_path.string());
  }

} // namespace tools::spritesmith
