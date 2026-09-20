// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/files.hpp>
#include <corundum/sprites/character_registry.hpp>
#include <corundum/sprites/character_sheet_loader.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace corundum::sprites {

  std::expected<void, std::string> CharacterRegistry::load_all(const fs::path &index_path) {
    const fs::path characters_dir = index_path / "characters";

    // Recurse so character sheets can be organized into subdirectories per character. The listing
    // is sorted, which keeps interned sheet/sprite ids stable across runs and platforms.
    const auto entries = core::list_dir_entries(characters_dir, {.extensions = {"json"}, .recursive = true});
    if (!entries)
      return std::unexpected(std::format("Characters sprite sheets are missing: {}", entries.error()));

    std::expected<void, std::string> failure;
    std::size_t sheet_count = 0;
    for (const auto &entry : *entries) {
      if (entry.is_dir)
        continue;
      auto result = load_sheet(entry.path);
      if (!result) {
        failure = std::unexpected(result.error());
        break;
      }
      ++sheet_count;
    }

    // Rebuild even on failure: a partial load has already mutated frames_, so the previous
    // pointers are dangling until this runs.
    rebuild_sprite_index();

    if (!failure.has_value())
      return failure;

    if (sheet_count == 0)
      return std::unexpected(std::format("Characters sprite sheets are missing: {}", characters_dir.string()));

    return {};
  }

  void CharacterRegistry::rebuild_sprite_index() {
    sprite_by_id_.assign(static_cast<std::size_t>(next_sprite_id_), nullptr);
    for (const Frames &frames : frames_.values())
      sprite_by_id_[frames.sprite_id] = &frames;
  }

  std::expected<void, std::string> CharacterRegistry::load_sheet(const fs::path &sheet_path) {
    auto result = load_character_sheet(sheet_path);
    if (!result)
      return std::unexpected(result.error());

    CharacterSheetData &data = *result;

    if (sheet_ids_.contains(data.id))
      return std::unexpected(std::format("Duplicate sheet id '{}' in '{}'", data.id, sheet_path.string()));

    // Validate every sprite name before mutating any state, so a collision leaves the registry as
    // it was rather than half-loaded.
    for (const auto &entry : data.sprites) {
      if (frames_.contains(entry.name))
        return std::unexpected(std::format("Duplicate sprite name '{}' in '{}'", entry.name, sheet_path.string()));
    }

    const Id sheet_id = next_id_++;
    sheet_ids_.emplace(data.id, sheet_id);
    sheets_by_id_.emplace(sheet_id, SpriteSheet{
                                        .id = sheet_id,
                                        .path = data.path,
                                        .frame_width = data.frame_width,
                                        .frame_height = data.frame_height,
                                        .offset_x = data.offset_x,
                                        .offset_y = data.offset_y,
                                        .spacing_x = data.spacing_x,
                                        .spacing_y = data.spacing_y,
                                    });

    // A sheet that omits the footprint keys silently gets the loader's default, which has
    // nothing to do with how big its art is. Say so: an unchosen hitbox should be visible in
    // the log rather than discovered in play.
    for (const auto &entry : data.sprites) {
      if (!entry.footprint_authored)
        std::println(stderr,
                     "[engine] WARN: sprite '{}/{}' declares no footprint; using {:.2f} x {:.2f} tiles — set "
                     "one in spritesmith",
                     data.id, entry.name, entry.footprint_col_span, entry.footprint_row_span);
    }

    for (auto &entry : data.sprites) {
      Frames frames;
      frames.sheet_id = sheet_id;
      frames.col_span = entry.col_span;
      frames.row_span = entry.row_span;
      frames.footprint_col_span = entry.footprint_col_span;
      frames.footprint_row_span = entry.footprint_row_span;
      frames.walk_around_offset = entry.walk_around_offset;
      frames.fps = entry.fps;
      frames.sprite_id = next_sprite_id_++;
      frames.animations = std::move(entry.animations);
      frames.anim_frames = std::move(entry.anim_frames);

      frames_.emplace(entry.name, std::move(frames));
    }

    return {};
  }

  Id CharacterRegistry::find_sheet(const std::string &id) const noexcept {
    auto it = sheet_ids_.find(id);
    return it != sheet_ids_.end() ? it->second : k_null_sheet;
  }

  const SpriteSheet *CharacterRegistry::get_sheet(Id id) const noexcept {
    auto it = sheets_by_id_.find(id);
    return it != sheets_by_id_.end() ? &it->second : nullptr;
  }

  const Frames *CharacterRegistry::get_sprite(const std::string &sprite_name) const noexcept {
    auto it = frames_.find(sprite_name);
    return it != frames_.end() ? &it->second : nullptr;
  }

  SpriteId CharacterRegistry::get_sprite_id(const std::string &sprite_name) const noexcept {
    auto it = frames_.find(sprite_name);
    return it != frames_.end() ? it->second.sprite_id : k_null_sprite_id;
  }

  const Frames *CharacterRegistry::get_sprite_by_id(SpriteId sid) const noexcept {
    if (sid == k_null_sprite_id)
      return nullptr;
    if (sid >= sprite_by_id_.size())
      return nullptr;
    return sprite_by_id_[sid];
  }

} // namespace corundum::sprites
