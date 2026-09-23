// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/world/tilemap/tilemap.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace corundum::world::tilemap {

  namespace {

    /// Appends an error for @p gid unless it is empty or owned by a tileset. Passing @p animation_name
    /// selects the animation-frame message; otherwise the placed-tile message is emitted.
    void append_orphaned_gid_error(const Tilemap &tilemap, const TilemapLayer &layer, int cell_index, TileId gid,
                                   const std::string *animation_name, std::vector<std::string> &errors) {
      if (gid == k_empty_tile || find_tileset(tilemap.tilesets, gid) != nullptr)
        return;

      const int col = cell_index % tilemap.width;
      const int row = cell_index / tilemap.width;
      if (animation_name != nullptr) {
        errors.push_back(
            std::format(R"(layer "{}" cell (col={}, row={}): animation "{}" frame GID {} has no matching tileset)",
                        layer.name, col, row, *animation_name, gid));
        return;
      }
      errors.push_back(std::format("layer \"{}\" cell (col={}, row={}): tile GID {} has no matching tileset",
                                   layer.name, col, row, gid));
    }

    void check_orphaned_gids(const Tilemap &tilemap, std::vector<std::string> &errors) {
      const std::size_t expected_tiles =
          static_cast<std::size_t>(tilemap.width) * static_cast<std::size_t>(tilemap.height);

      for (const auto &layer : tilemap.layers) {
        if (layer.tiles.size() != expected_tiles) {
          errors.push_back(std::format("layer \"{}\" has {} tiles, expected {} (width * height)", layer.name,
                                       layer.tiles.size(), expected_tiles));
          continue;
        }

        for (std::size_t index = 0; index < layer.tiles.size(); ++index) {
          if (layer.animated_cells.contains(static_cast<int>(index)))
            continue;
          append_orphaned_gid_error(tilemap, layer, static_cast<int>(index), layer.tiles[index], nullptr, errors);
        }

        for (const auto &[cell_index, animation] : layer.animated_cells) {
          for (const TileId gid : animation.frame_gids)
            append_orphaned_gid_error(tilemap, layer, cell_index, gid, &animation.anim_name, errors);
        }
      }
    }

    void check_duplicate_layer_names(const Tilemap &tilemap, std::vector<std::string> &errors) {
      std::unordered_set<std::string> seen;
      for (const auto &layer : tilemap.layers) {
        if (layer.name.empty())
          continue;
        if (!seen.insert(layer.name).second)
          errors.push_back(std::format("duplicate layer name \"{}\"", layer.name));
      }
    }

    bool rect_out_of_bounds(float col, float row, float col_span, float row_span, const Tilemap &tilemap) {
      if (!std::isfinite(col) || !std::isfinite(row) || !std::isfinite(col_span) || !std::isfinite(row_span))
        return true;
      if (col_span < 0.f || row_span < 0.f)
        return true;
      return col < 0.f || row < 0.f || col + col_span > static_cast<float>(tilemap.width) ||
             row + row_span > static_cast<float>(tilemap.height);
    }

    /// Reports every shape whose SoA fields fall outside the map; @p noun names the shape in the message.
    /// Iterates the shortest parallel span so a mis-sized container cannot index out of bounds.
    template <typename CollisionShapes>
    void check_shape_bounds(const CollisionShapes &shapes, std::string_view noun, const Tilemap &tilemap,
                            std::vector<std::string> &errors) {
      const std::size_t count =
          std::min({shapes.cols.size(), shapes.rows.size(), shapes.col_spans.size(), shapes.row_spans.size()});
      for (std::size_t i = 0; i < count; ++i) {
        if (rect_out_of_bounds(shapes.cols[i], shapes.rows[i], shapes.col_spans[i], shapes.row_spans[i], tilemap))
          errors.push_back(std::format("collision {} at (col={}, row={}) extends outside the {}x{} map", noun,
                                       shapes.cols[i], shapes.rows[i], tilemap.width, tilemap.height));
      }
    }

    void check_collision_bounds(const Tilemap &tilemap, std::vector<std::string> &errors) {
      check_shape_bounds(tilemap.collisions, "rect", tilemap, errors);
      check_shape_bounds(tilemap.collision_triangles, "triangle", tilemap, errors);
    }

    void check_ramp_bounds(const Tilemap &tilemap, std::vector<std::string> &errors) {
      for (const auto &layer : tilemap.layers) {
        for (const auto &[index, axis] : layer.ramps) {
          const int col = index % tilemap.width;
          const int row = index / tilemap.width;
          const int delta_col = axis == RampAxis::NorthSouth ? 0 : 1;
          const int delta_row = axis == RampAxis::NorthSouth ? -1 : 0;
          const int col_a = col + delta_col;
          const int row_a = row + delta_row;
          const int col_b = col - delta_col;
          const int row_b = row - delta_row;
          if (col_a < 0 || row_a < 0 || col_a >= tilemap.width || row_a >= tilemap.height || col_b < 0 || row_b < 0 ||
              col_b >= tilemap.width || row_b >= tilemap.height)
            errors.push_back(std::format("layer \"{}\" ramp at (col={}, row={}) has an axis-neighbor outside the "
                                         "{}x{} map",
                                         layer.name, col, row, tilemap.width, tilemap.height));
        }
      }
    }

  } // namespace

  std::vector<std::string> validate(const Tilemap &tilemap) {
    std::vector<std::string> errors;
    if (tilemap.width <= 0 || tilemap.height <= 0) {
      errors.push_back(std::format("invalid map dimensions {}x{}", tilemap.width, tilemap.height));
      return errors;
    }

    check_orphaned_gids(tilemap, errors);
    check_duplicate_layer_names(tilemap, errors);
    check_collision_bounds(tilemap, errors);
    check_ramp_bounds(tilemap, errors);
    return errors;
  }

} // namespace corundum::world::tilemap
