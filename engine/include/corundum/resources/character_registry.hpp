#pragma once
#include <corundum/resources/sprite.hpp>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <string>

namespace corundum::resources {

  /// Owns all loaded sprite sheets and per-sprite frame data for characters.
  class CharacterRegistry {
  public:
    /// Scans index_path/characters/ and loads every sprite sheet JSON found.
    /// @return std::unexpected with error message if any sheet is missing, malformed, or contains duplicate ids.
    [[nodiscard]] std::expected<void, std::string> load_all(const std::filesystem::path &index_path);

    /// Returns the numeric Id for a sheet string name, or k_null_sheet if not found.
    [[nodiscard]] Id find_sheet(const std::string &id) const noexcept;

    /// Returns a pointer to the sheet with the given Id, or nullptr if not found.
    [[nodiscard]] const SpriteSheet *get_sheet(Id id) const noexcept;

    /// Returns a pointer to the Frames for the given sprite name, or nullptr if not found.
    [[nodiscard]] const Frames *get_sprite(const std::string &sprite_name) const noexcept;

    /// Resolves a sprite name to its interned SpriteId; returns k_null_sprite_id if unknown.
    [[nodiscard]] SpriteId get_sprite_id(const std::string &sprite_name) const noexcept;

    /// O(1) direct lookup by interned SpriteId; returns nullptr for null or out-of-range ids.
    [[nodiscard]] const Frames *get_sprite_by_id(SpriteId sid) const noexcept;

    /// All loaded sheets keyed by numeric Id.
    [[nodiscard]] const std::flat_map<Id, SpriteSheet> &sheets() const noexcept {
      return sheets_by_id_;
    }

    /// All loaded frame data keyed by sprite name.
    [[nodiscard]] const std::flat_map<std::string, Frames> &frames() const noexcept {
      return frames_;
    }

  private:
    std::expected<void, std::string> load_sheet(const std::filesystem::path &sheet_path);

    Id next_id_ = 1;
    std::flat_map<std::string, Id> sheet_ids_;
    std::flat_map<Id, SpriteSheet> sheets_by_id_;
    std::flat_map<std::string, Frames> frames_;

    SpriteId next_sprite_id_ = 1;
    std::vector<const Frames *> sprite_by_id_;
  };

} // namespace corundum::resources
