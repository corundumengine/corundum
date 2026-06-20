#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Pure, I/O-free tile-layer encoding functions shared by tools/import_tiled
// and tools/tilesmith/save.cpp.  No GLFW, no ImGui, no tilesmith-internal
// includes.

namespace tools::tilemap {

  inline constexpr int EMPTY_TILE_TILED = 0;
  inline constexpr int EMPTY_TILE_PROJECT = 0xFFFF; // 65535
  inline constexpr uint32_t FLIP_BITS_MASK = 0xF0000000u;
  inline constexpr double DEFAULT_SPARSE_THRESHOLD = 0.6;

  /**
   * @brief Decide whether to use sparse encoding for a layer.
   *
   * Returns true when the fraction of EMPTY_TILE_PROJECT values in
   * @p project_gids exceeds @p threshold. An empty vector is treated as
   * fully sparse.
   *
   * @param project_gids Flat, row-major array of project GIDs.
   * @param threshold    Fraction of empty tiles above which sparse is preferred.
   * @return True if sparse encoding is recommended.
   */
  [[nodiscard]] inline bool should_use_sparse(const std::vector<int> &project_gids, double threshold) {
    if (project_gids.empty())
      return true;
    int empty_count = 0;
    for (int g : project_gids)
      if (g == EMPTY_TILE_PROJECT)
        ++empty_count;
    return (static_cast<double>(empty_count) / static_cast<double>(project_gids.size())) > threshold;
  }

  /**
   * @brief Encode project GIDs as an array of row strings (dense format).
   *
   * Each element is a comma-separated string of @p width integer values.
   * The result contains @p height rows in row-major order.
   *
   * @param project_gids Flat, row-major array of project GIDs.
   * @param width        Map width in tiles.
   * @param height       Map height in tiles.
   * @return Vector of @p height comma-separated row strings.
   */
  [[nodiscard]] inline std::vector<std::string> convert_layer_dense(const std::vector<int> &project_gids, int width,
                                                                    int height) {
    std::vector<std::string> rows;
    rows.reserve(static_cast<std::size_t>(height));
    for (int r = 0; r < height; ++r) {
      std::string row;
      for (int c = 0; c < width; ++c) {
        if (c > 0)
          row += ',';
        row += std::to_string(project_gids[static_cast<std::size_t>(r * width + c)]);
      }
      rows.push_back(std::move(row));
    }
    return rows;
  }

  /**
   * @brief Encode non-empty project GIDs as a JSON array of {col, row, id} objects.
   *
   * Cells equal to EMPTY_TILE_PROJECT are omitted. Cells are visited in
   * row-major order.
   *
   * @param project_gids Flat, row-major array of project GIDs.
   * @param width        Map width in tiles.
   * @param height       Map height in tiles.
   * @return JSON array of sparse tile objects.
   */
  [[nodiscard]] inline nlohmann::json convert_layer_sparse(const std::vector<int> &project_gids, int width,
                                                           int height) {
    nlohmann::json objects = nlohmann::json::array();
    for (int idx = 0; idx < width * height; ++idx) {
      const int gid = project_gids[static_cast<std::size_t>(idx)];
      if (gid == EMPTY_TILE_PROJECT)
        continue;
      objects.push_back({{"col", idx % width}, {"row", idx / width}, {"id", gid}});
    }
    return objects;
  }

} // namespace tools::tilemap
