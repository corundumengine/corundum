#include <corundum/gameplay/world/tilemap/tilemap.hpp>

#include <format>
#include <unordered_set>
#include <vector>

namespace corundum::gameplay::world::tilemap {

  namespace {

    void check_orphaned_gids(const Tilemap &tm, std::vector<std::string> &errors) {
      for (std::size_t li = 0; li < tm.layers.size(); ++li) {
        const auto &layer = tm.layers[li];

        for (std::size_t idx = 0; idx < layer.tiles.size(); ++idx) {
          const TileId gid = layer.tiles[idx];
          if (gid == k_empty_tile || layer.animated_cells.contains(static_cast<int>(idx)))
            continue;
          if (find_tileset(tm.tilesets, gid) != nullptr)
            continue;
          const int col = static_cast<int>(idx) % tm.width;
          const int row = static_cast<int>(idx) / tm.width;
          errors.push_back(std::format("layer \"{}\" cell (col={}, row={}): tile GID {} has no matching tileset",
                                       layer.name, col, row, gid));
        }

        for (const auto &[cell_idx, anim] : layer.animated_cells) {
          for (const TileId gid : anim.frame_gids) {
            if (gid == k_empty_tile)
              continue;
            if (find_tileset(tm.tilesets, gid) != nullptr)
              continue;
            const int col = cell_idx % tm.width;
            const int row = cell_idx / tm.width;
            errors.push_back(
                std::format("layer \"{}\" cell (col={}, row={}): animation \"{}\" frame GID {} has no matching tileset",
                            layer.name, col, row, anim.anim_name, gid));
          }
        }
      }
    }

    void check_duplicate_layer_names(const Tilemap &tm, std::vector<std::string> &errors) {
      std::unordered_set<std::string> seen;
      for (const auto &layer : tm.layers) {
        if (layer.name.empty())
          continue;
        if (!seen.insert(layer.name).second)
          errors.push_back(std::format("duplicate layer name \"{}\"", layer.name));
      }
    }

    bool rect_out_of_bounds(float col, float row, float col_span, float row_span, const Tilemap &tm) {
      return col < 0.f || row < 0.f || col + col_span > static_cast<float>(tm.width) ||
             row + row_span > static_cast<float>(tm.height);
    }

    void check_collision_bounds(const Tilemap &tm, std::vector<std::string> &errors) {
      const auto &cr = tm.collisions;
      for (std::size_t i = 0; i < cr.size(); ++i) {
        if (rect_out_of_bounds(cr.cols[i], cr.rows[i], cr.col_spans[i], cr.row_spans[i], tm))
          errors.push_back(std::format("collision rect at (col={}, row={}) extends outside the {}x{} map", cr.cols[i],
                                       cr.rows[i], tm.width, tm.height));
      }

      const auto &ct = tm.collision_triangles;
      for (std::size_t i = 0; i < ct.size(); ++i) {
        if (rect_out_of_bounds(ct.cols[i], ct.rows[i], ct.col_spans[i], ct.row_spans[i], tm))
          errors.push_back(std::format("collision triangle at (col={}, row={}) extends outside the {}x{} map",
                                       ct.cols[i], ct.rows[i], tm.width, tm.height));
      }
    }

  } // namespace

  std::vector<std::string> validate(const Tilemap &tm) {
    std::vector<std::string> errors;
    check_orphaned_gids(tm, errors);
    check_duplicate_layer_names(tm, errors);
    check_collision_bounds(tm, errors);
    return errors;
  }

} // namespace corundum::gameplay::world::tilemap
