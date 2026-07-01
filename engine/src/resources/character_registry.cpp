#include <corundum/core/files.hpp>
#include <corundum/resources/character_registry.hpp>
#include <corundum/resources/character_sheet_loader.hpp>
#include <corundum/resources/sprite.hpp>

namespace fs = std::filesystem;

namespace corundum::resources {

  std::expected<void, std::string> CharacterRegistry::load_all(const fs::path &index_path) {
    const fs::path characters_dir = index_path / "characters";

    // Recurse so character sheets can be organized into subdirectories per character.
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(characters_dir, ec)) {
      if (!ec && entry.is_regular_file() && entry.path().extension() == ".json")
        files.push_back(entry.path());
    }

    if (files.empty())
      return std::unexpected(std::format("Characters sprite sheets are missing: {}", characters_dir.string()));

    for (const auto &file : files) {
      auto result = load_sheet(fs::path(file));
      if (!result)
        return std::unexpected(result.error());
    }

    // Populate sprite_by_id_ after all load_sheet() calls complete
    sprite_by_id_.resize(next_sprite_id_, nullptr);
    for (auto &&[sprite_name, frames] : frames_) {
      const auto sid = get_sprite_id(sprite_name);
      if (sid != k_null_sprite_id) {
        sprite_by_id_[sid] = &frames;
      }
    }

    return {};
  }

  std::expected<void, std::string> CharacterRegistry::load_sheet(const fs::path &sheet_path) {
    auto result = load_character_sheet(sheet_path);
    if (!result)
      return std::unexpected(result.error());

    CharacterSheetData &data = *result;

    if (sheet_ids_.contains(data.id))
      return std::unexpected(std::format("Duplicate sheet id: '{}'", data.id));

    const Id sheet_id = next_id_++;
    sheet_ids_.emplace(data.id, sheet_id);
    sheets_by_id_.emplace(sheet_id, SpriteSheet{sheet_id, data.path, data.frame_width, data.frame_height, data.offset_x,
                                                data.offset_y, data.spacing_x, data.spacing_y});

    for (auto &entry : data.sprites) {
      if (frames_.contains(entry.name))
        return std::unexpected(std::format("Duplicate sprite name: '{}'", entry.name));

      Frames frames;
      frames.sheet_id = sheet_id;
      frames.col_span = entry.col_span;
      frames.row_span = entry.row_span;
      frames.collision_w = entry.collision_w;
      frames.collision_h = entry.collision_h;
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

} // namespace corundum::resources
