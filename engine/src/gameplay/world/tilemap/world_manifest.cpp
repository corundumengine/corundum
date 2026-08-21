#include <corundum/gameplay/world/tilemap/world_manifest.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace corundum::gameplay::world::tilemap {

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

    auto require_pos_int = [&](const char *key) -> std::expected<int, std::string> {
      if (!j.contains(key))
        return std::unexpected(std::format("Manifest '{}' missing '{}'", path.string(), key));
      int v{};
      try {
        v = j[key].get<int>();
      } catch (...) {
        return std::unexpected(std::format("Manifest field '{}' has wrong type", key));
      }
      if (v <= 0)
        return std::unexpected(std::format("Manifest field '{}' must be positive", key));
      return v;
    };

    WorldManifest m;
    if (auto v = require_pos_int("chunk_size")) {
      m.chunk_size = *v;
    } else {
      return std::unexpected(v.error());
    }
    if (auto v = require_pos_int("chunks_wide")) {
      m.chunks_wide = *v;
    } else {
      return std::unexpected(v.error());
    }
    if (auto v = require_pos_int("chunks_tall")) {
      m.chunks_tall = *v;
    } else {
      return std::unexpected(v.error());
    }
    m.tiles_wide = j.value("tiles_wide", m.chunks_wide * m.chunk_size);
    m.tiles_tall = j.value("tiles_tall", m.chunks_tall * m.chunk_size);
    m.base_dir = path.parent_path();
    return m;
  }

  ChunkCoord chunk_at_cart(float wx, float wy, const WorldManifest &m, int tile_px, float tile_scale) noexcept {
    const float chunk_px = static_cast<float>(m.chunk_size * tile_px) * tile_scale;
    const int cx = static_cast<int>(wx / chunk_px);
    const int cy = static_cast<int>(wy / chunk_px);
    return {std::clamp(cx, 0, m.chunks_wide - 1), std::clamp(cy, 0, m.chunks_tall - 1)};
  }

  ChunkCoord chunk_at_iso(float iso_x, float iso_y, const WorldManifest &m,
                          const corundum::core::math::IsometricParams &iso) noexcept {
    // Invert the isometric projection (delegating to world_to_tile so x_origin and
    // elev_step are handled the same way as every other consumer of the projection),
    // then std::floor the fractional tile onto its containing chunk. floor — not
    // truncate — to match the rest of the codebase (picking, tilesmith).
    const auto tile = corundum::core::math::world_to_tile({iso_x, iso_y}, /*elevation=*/0, iso);
    const float chunk_f = static_cast<float>(m.chunk_size);
    const int cx = static_cast<int>(std::floor(tile.x / chunk_f));
    const int cy = static_cast<int>(std::floor(tile.y / chunk_f));
    return {std::clamp(cx, 0, m.chunks_wide - 1), std::clamp(cy, 0, m.chunks_tall - 1)};
  }

  std::pair<float, float> chunk_origin_px(ChunkCoord c, const WorldManifest &m, int tile_px,
                                          float tile_scale) noexcept {
    const float chunk_px = static_cast<float>(m.chunk_size * tile_px) * tile_scale;
    return {static_cast<float>(c.col) * chunk_px, static_cast<float>(c.row) * chunk_px};
  }

  std::pair<float, float> world_bounds_iso(const WorldManifest &m, float half_tw, float half_th) noexcept {
    const int tw = m.tiles_wide > 0 ? m.tiles_wide : m.chunks_wide * m.chunk_size;
    const int th = m.tiles_tall > 0 ? m.tiles_tall : m.chunks_tall * m.chunk_size;
    const float steps = static_cast<float>(tw + th - 1);
    return {steps * half_tw * 2.f, steps * half_th * 2.f};
  }

  std::vector<ChunkCoord> active_chunk_coords(ChunkCoord center, int radius, const WorldManifest &m) {
    std::vector<ChunkCoord> result;
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        ChunkCoord c{center.col + dx, center.row + dy};
        if (m.in_bounds(c))
          result.push_back(c);
      }
    }
    return result;
  }

} // namespace corundum::gameplay::world::tilemap
