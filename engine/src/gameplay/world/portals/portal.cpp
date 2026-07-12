#include <corundum/gameplay/world/portals/portal.hpp>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace corundum::gameplay::world {

  std::expected<std::vector<Portal>, std::string> load_portals(const std::string &path) {
    std::ifstream f(path);
    if (!f)
      return {};

    json j;
    try {
      j = json::parse(f);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed portals '{}': {}", path, e.what()));
    }

    if (!j.is_object())
      return std::unexpected(std::format("Portals '{}' must be a JSON object", path));

    if (!j.contains("portals") || !j["portals"].is_array())
      return std::unexpected(std::format("Portals '{}' missing 'portals' array", path));

    const auto &arr = j["portals"];
    std::vector<Portal> result;
    result.reserve(arr.size());

    for (std::size_t i = 0; i < arr.size(); ++i) {
      const auto &entry = arr[i];

      if (!entry.is_object())
        return std::unexpected(std::format("Portals '{}' portals[{}] must be an object", path, i));

      int col, row, w, h;
      try {
        col = entry.at("col").get<int>();
        row = entry.at("row").get<int>();
        w = entry.at("w").get<int>();
        h = entry.at("h").get<int>();
      } catch (...) {
        return std::unexpected(
            std::format("Portals '{}' portals[{}] missing or invalid 'col', 'row', 'w', or 'h'", path, i));
      }
      if (col < 0 || row < 0 || w <= 0 || h <= 0)
        return std::unexpected(
            std::format("Portals '{}' portals[{}] 'col'/'row' must be >= 0 and 'w'/'h' must be > 0", path, i));

      int target_chunk_x = -1;
      int target_chunk_y = -1;
      if (entry.contains("target_chunk_x")) {
        try {
          target_chunk_x = entry.at("target_chunk_x").get<int>();
          target_chunk_y = entry.at("target_chunk_y").get<int>();
        } catch (...) {
          return std::unexpected(
              std::format("Portals '{}' portals[{}] invalid 'target_chunk_x'/'target_chunk_y'", path, i));
        }
        if (target_chunk_x < 0 || target_chunk_y < 0)
          return std::unexpected(
              std::format("Portals '{}' portals[{}] 'target_chunk_x'/'target_chunk_y' must be >= 0", path, i));
      }

      std::string target_map;
      if (entry.contains("target_map")) {
        try {
          target_map = entry.at("target_map").get<std::string>();
        } catch (...) {
          return std::unexpected(std::format("Portals '{}' portals[{}] invalid 'target_map'", path, i));
        }
      }
      if (target_chunk_x < 0 && target_map.empty())
        return std::unexpected(std::format(
            "Portals '{}' portals[{}] must have 'target_map' or 'target_chunk_x'/'target_chunk_y'", path, i));

      int spawn_col, spawn_row;
      try {
        spawn_col = entry.at("spawn_col").get<int>();
        spawn_row = entry.at("spawn_row").get<int>();
      } catch (...) {
        return std::unexpected(
            std::format("Portals '{}' portals[{}] missing or invalid 'spawn_col'/'spawn_row'", path, i));
      }
      if (spawn_col < 0 || spawn_row < 0)
        return std::unexpected(std::format("Portals '{}' portals[{}] 'spawn_col'/'spawn_row' must be >= 0", path, i));

      result.push_back(Portal{
          static_cast<float>(col),
          static_cast<float>(row),
          static_cast<float>(w),
          static_cast<float>(h),
          std::move(target_map),
          spawn_col,
          spawn_row,
          target_chunk_x,
          target_chunk_y,
      });
    }

    return result;
  }

} // namespace corundum::gameplay::world
