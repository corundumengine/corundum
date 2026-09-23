// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <cmath>
#include <corundum/core/math/isometric.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>
#include <corundum/world/world_bounds.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace corundum::world::tilemap {

  namespace {

    /// Reads required @p key as a positive int, reporting a missing key, wrong type or
    /// non-positive value as an error string rather than letting nlohmann throw out of
    /// the load path.
    std::expected<int, std::string> read_positive_int(const json &object, const fs::path &path, const char *key) {
      if (!object.contains(key))
        return std::unexpected(std::format("Manifest '{}' missing '{}'", path.string(), key));
      int value{};
      try {
        value = object[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Manifest '{}' field '{}' has wrong type", path.string(), key));
      }
      if (value <= 0)
        return std::unexpected(std::format("Manifest '{}' field '{}' must be positive", path.string(), key));
      return value;
    }

    /// Reads optional @p key as a positive int, returning @p default_value when absent.
    std::expected<int, std::string> read_optional_positive_int(const json &object, const fs::path &path,
                                                               const char *key, int default_value) {
      if (!object.contains(key))
        return default_value;
      return read_positive_int(object, path, key);
    }

  } // namespace

  std::filesystem::path WorldManifest::chunk_path(ChunkCoord c) const {
    return base_dir / std::format("chunk_{}_{}.json", c.col, c.row);
  }

  std::expected<WorldManifest, std::string> load_world_manifest(const fs::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("Cannot open world manifest: {}", path.string()));

    json j;
    try {
      j = json::parse(f, nullptr, true, true);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("Malformed manifest {}: {}", path.string(), e.what()));
    }

    WorldManifest m;
    const std::array required_fields{
        std::pair{"chunk_size", &m.chunk_size},
        std::pair{"chunks_wide", &m.chunks_wide},
        std::pair{"chunks_tall", &m.chunks_tall},
    };
    for (const auto &[key, member] : required_fields) {
      const auto value = read_positive_int(j, path, key);
      if (!value)
        return std::unexpected(value.error());
      *member = *value;
    }

    const auto tiles_wide = read_optional_positive_int(j, path, "tiles_wide", m.chunks_wide * m.chunk_size);
    if (!tiles_wide)
      return std::unexpected(tiles_wide.error());
    m.tiles_wide = *tiles_wide;

    const auto tiles_tall = read_optional_positive_int(j, path, "tiles_tall", m.chunks_tall * m.chunk_size);
    if (!tiles_tall)
      return std::unexpected(tiles_tall.error());
    m.tiles_tall = *tiles_tall;

    m.base_dir = path.parent_path();
    return m;
  }

  ChunkCoord chunk_at_cart(float wx, float wy, const WorldManifest &m, int tile_px, float tile_scale) noexcept {
    if (m.chunk_size <= 0 || m.chunks_wide <= 0 || m.chunks_tall <= 0 || tile_px <= 0 || tile_scale <= 0.f)
      return {};
    const float chunk_px = static_cast<float>(m.chunk_size * tile_px) * tile_scale;
    const int cx = static_cast<int>(wx / chunk_px);
    const int cy = static_cast<int>(wy / chunk_px);
    return {.col = std::clamp(cx, 0, m.chunks_wide - 1), .row = std::clamp(cy, 0, m.chunks_tall - 1)};
  }

  ChunkCoord chunk_at_iso(float iso_x, float iso_y, const WorldManifest &m,
                          const corundum::core::math::IsometricParams &iso) noexcept {
    if (m.chunk_size <= 0 || m.chunks_wide <= 0 || m.chunks_tall <= 0)
      return {};
    // Invert the isometric projection (delegating to world_to_tile so x_origin and
    // elev_step are handled the same way as every other consumer of the projection),
    // then std::floor the fractional tile onto its containing chunk. floor — not
    // truncate — to match the rest of the codebase (picking, tilesmith).
    const auto tile = corundum::core::math::world_to_tile({.x = iso_x, .y = iso_y}, /*elevation=*/0, iso);
    const float chunk_f = static_cast<float>(m.chunk_size);
    const int cx = static_cast<int>(std::floor(tile.x / chunk_f));
    const int cy = static_cast<int>(std::floor(tile.y / chunk_f));
    return {.col = std::clamp(cx, 0, m.chunks_wide - 1), .row = std::clamp(cy, 0, m.chunks_tall - 1)};
  }

  std::pair<float, float> chunk_origin_px(ChunkCoord c, const WorldManifest &m, int tile_px,
                                          float tile_scale) noexcept {
    const float chunk_px = static_cast<float>(m.chunk_size * tile_px) * tile_scale;
    return {static_cast<float>(c.col) * chunk_px, static_cast<float>(c.row) * chunk_px};
  }

  WorldBounds world_bounds_for_tiles(int tiles_wide, int tiles_tall, float half_tw, float half_th) noexcept {
    const float steps{static_cast<float>(tiles_wide + tiles_tall - 1)};
    return {.width_px = steps * half_tw * 2.f, .height_px = steps * half_th * 2.f};
  }

  std::pair<float, float> world_bounds_iso(const WorldManifest &m, float half_tw, float half_th) noexcept {
    const WorldBounds bounds{
        world_bounds_for_tiles(m.effective_tiles_wide(), m.effective_tiles_tall(), half_tw, half_th),
    };
    return {bounds.width_px, bounds.height_px};
  }

  std::vector<ChunkCoord> active_chunk_coords(ChunkCoord center, int radius, const WorldManifest &m) {
    std::vector<ChunkCoord> result;
    if (radius > 0) {
      const int side = (2 * radius) + 1;
      result.reserve(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    }
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        const ChunkCoord c{.col = center.col + dx, .row = center.row + dy};
        if (m.in_bounds(c))
          result.push_back(c);
      }
    }
    return result;
  }

} // namespace corundum::world::tilemap
