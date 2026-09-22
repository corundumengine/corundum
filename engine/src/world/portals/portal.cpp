// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_io.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/world/portals/portal.hpp>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::world {

  namespace {

    /// Migration step for portals documents. No migrations exist yet — schema_version 1
    /// is both the legacy (absent-field) format and the current format — so this returns
    /// @p from_version unchanged. A future version bump must advance it and never edit a
    /// shipped step, or prepare_schema_version rejects the result as an incomplete migration.
    std::expected<int, std::string> migrate_portals_json(json & /*root*/, int from_version,
                                                         const std::string & /*path*/) {
      return from_version;
    }

    /// Trigger rectangle in tile-grid units, prior to the float conversion Portal stores.
    struct PortalRect {
      int col = 0;
      int row = 0;
      int w = 0;
      int h = 0;
    };

    /// Read the required trigger rectangle, validating its extent. @p context prefixes errors.
    std::expected<PortalRect, std::string> read_rect(const json &entry, const std::string &context) {
      try {
        PortalRect rect;
        rect.col = entry.at("col").get<int>();
        rect.row = entry.at("row").get<int>();
        rect.w = entry.at("w").get<int>();
        rect.h = entry.at("h").get<int>();
        if (rect.col < 0 || rect.row < 0 || rect.w <= 0 || rect.h <= 0)
          return std::unexpected(std::format("{} 'col'/'row' must be >= 0 and 'w'/'h' must be > 0", context));
        return rect;
      } catch (const json::exception &) {
        return std::unexpected(std::format("{} missing or invalid 'col', 'row', 'w', or 'h'", context));
      }
    }

    /// Resolve a world-mode chunk target from the current or legacy key names. Returns
    /// (-1, -1) when neither spelling is present.
    std::expected<std::pair<int, int>, std::string> read_chunk_target(const json &entry, const std::string &context) {
      const char *col_key = nullptr;
      const char *row_key = nullptr;
      if (entry.contains("target_chunk_col")) {
        col_key = "target_chunk_col";
        row_key = "target_chunk_row";
      } else if (entry.contains("target_chunk_x")) {
        col_key = "target_chunk_x";
        row_key = "target_chunk_y";
      } else {
        return std::pair{-1, -1};
      }

      try {
        const int col = entry.at(col_key).get<int>();
        const int row = entry.at(row_key).get<int>();
        if (col < 0 || row < 0)
          return std::unexpected(std::format("{} '{}'/'{}' must be >= 0", context, col_key, row_key));
        return std::pair{col, row};
      } catch (const json::exception &) {
        return std::unexpected(std::format("{} invalid '{}'/'{}'", context, col_key, row_key));
      }
    }

    /// Read optional scalar @p key as @p T, defaulting to @p fallback when absent.
    template <typename T>
    std::expected<T, std::string> read_optional(const json &entry, const char *key, T fallback,
                                                const std::string &context) {
      if (!entry.contains(key))
        return fallback;
      try {
        return entry.at(key).get<T>();
      } catch (const json::exception &) {
        return std::unexpected(std::format("{} invalid '{}'", context, key));
      }
    }

    /// Read the required spawn tile, validating that it is non-negative.
    std::expected<std::pair<int, int>, std::string> read_spawn(const json &entry, const std::string &context) {
      try {
        const int col = entry.at("spawn_col").get<int>();
        const int row = entry.at("spawn_row").get<int>();
        if (col < 0 || row < 0)
          return std::unexpected(std::format("{} 'spawn_col'/'spawn_row' must be >= 0", context));
        return std::pair{col, row};
      } catch (const json::exception &) {
        return std::unexpected(std::format("{} missing or invalid 'spawn_col'/'spawn_row'", context));
      }
    }

    /// Build one Portal from a JSON array entry, applying the same validation the file had
    /// when it was hand-authored. @p context is "Portals '<path>' portals[<i>]".
    std::expected<Portal, std::string> read_portal(const json &entry, const std::string &context) {
      if (!entry.is_object())
        return std::unexpected(std::format("{} must be an object", context));

      const auto rect = read_rect(entry, context);
      if (!rect)
        return std::unexpected(rect.error());

      const auto chunk = read_chunk_target(entry, context);
      if (!chunk)
        return std::unexpected(chunk.error());

      const auto target_map = read_optional<std::string>(entry, "target_map", "", context);
      if (!target_map)
        return std::unexpected(target_map.error());

      const auto return_to_world = read_optional<bool>(entry, "return_to_world", false, context);
      if (!return_to_world)
        return std::unexpected(return_to_world.error());

      const auto [chunk_col, chunk_row] = *chunk;
      if (chunk_col < 0 && target_map->empty() && !*return_to_world)
        return std::unexpected(std::format(
            "{} must have 'target_map' or 'target_chunk_col'/'target_chunk_row' or 'return_to_world'", context));

      const auto spawn = read_spawn(entry, context);
      if (!spawn)
        return std::unexpected(spawn.error());

      return Portal{
          .col = static_cast<float>(rect->col),
          .row = static_cast<float>(rect->row),
          .w = static_cast<float>(rect->w),
          .h = static_cast<float>(rect->h),
          .target_map = *target_map,
          .spawn_col = spawn->first,
          .spawn_row = spawn->second,
          .target_chunk_col = chunk_col,
          .target_chunk_row = chunk_row,
          .return_to_world = *return_to_world,
      };
    }

  } // namespace

  std::expected<std::vector<Portal>, std::string> load_portals(const std::filesystem::path &path) {
    // Absent file is a legitimate empty portal list; any other open/parse failure is an error.
    if (!std::filesystem::exists(path))
      return {};

    const std::string path_str = path.string();

    auto parsed = corundum::core::read_json(path);
    if (!parsed)
      return std::unexpected(std::move(parsed).error());
    json j = std::move(*parsed);

    auto prepared =
        corundum::core::prepare_schema_version(j, k_portals_schema_version, "Portals", path_str, migrate_portals_json);
    if (!prepared)
      return std::unexpected(std::move(prepared).error());

    if (!j.is_object())
      return std::unexpected(std::format("Portals '{}' must be a JSON object", path_str));

    if (!j.contains("portals") || !j["portals"].is_array())
      return std::unexpected(std::format("Portals '{}' missing 'portals' array", path_str));

    const auto &arr = j["portals"];
    std::vector<Portal> result;
    result.reserve(arr.size());

    for (std::size_t i = 0; i < arr.size(); ++i) {
      auto portal = read_portal(arr[i], std::format("Portals '{}' portals[{}]", path_str, i));
      if (!portal)
        return std::unexpected(std::move(portal).error());
      result.push_back(std::move(*portal));
    }

    return result;
  }

  nlohmann::json serialize(const std::vector<Portal> &portals) {
    nlohmann::json j;
    j["schema_version"] = k_portals_schema_version;
    j["portals"] = nlohmann::json::array();
    for (const auto &p : portals) {
      nlohmann::json pj;
      pj["col"] = static_cast<int>(p.col);
      pj["row"] = static_cast<int>(p.row);
      pj["w"] = static_cast<int>(p.w);
      pj["h"] = static_cast<int>(p.h);
      if (!p.target_map.empty())
        pj["target_map"] = p.target_map;
      pj["spawn_col"] = p.spawn_col;
      pj["spawn_row"] = p.spawn_row;
      if (p.target_chunk_col >= 0 && p.target_chunk_row >= 0) {
        pj["target_chunk_col"] = p.target_chunk_col;
        pj["target_chunk_row"] = p.target_chunk_row;
      }
      if (p.return_to_world)
        pj["return_to_world"] = true;
      j["portals"].push_back(std::move(pj));
    }
    return j;
  }

} // namespace corundum::world
