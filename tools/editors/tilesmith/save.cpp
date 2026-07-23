#include "save.hpp"
#include "portal_entry.hpp"

#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/tilemap/serialize.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools::tilemap {

  std::expected<void, std::string> save_tilemap(EditorState &state) {
    // 1. Read existing JSON for base-merge (preserves unknown keys)
    std::ifstream in(state.map_path);
    if (!in)
      return std::unexpected("Cannot open: " + state.map_path.string());
    nlohmann::json base = nlohmann::json::parse(in, nullptr, true, true);

    // 2. Serialize tilemap onto base
    nlohmann::json j = corundum::gameplay::world::tilemap::serialize_tilemap(state.map, &base);

    // 3. Write tilemap
    {
      auto res = corundum::core::write_json(state.map_path, j);
      if (!res)
        return std::unexpected(res.error());
    }

    // 4. Save portals via engine serializer
    {
      std::vector<corundum::gameplay::world::Portal> engine_portals;
      engine_portals.reserve(state.portals.size());
      for (const auto &pe : state.portals)
        engine_portals.push_back({static_cast<float>(pe.col), static_cast<float>(pe.row), static_cast<float>(pe.w),
                                  static_cast<float>(pe.h), pe.target_map, pe.spawn_col, pe.spawn_row});

      nlohmann::json portals_json = corundum::gameplay::world::serialize_portals(engine_portals);
      const auto ppath = portals_path(state.map_path);
      std::filesystem::create_directories(ppath.parent_path());
      auto res = corundum::core::write_json(ppath, portals_json);
      if (!res)
        return std::unexpected(res.error());
    }

    // 5. Save tiledata sidecars via engine serializer
    for (const auto &saved_ts : state.map.tilesets) {
      if (saved_ts.info.source.empty())
        continue;
      nlohmann::json sc = corundum::gameplay::world::tilemap::serialize_tiledata(saved_ts.info);
      const std::filesystem::path source = saved_ts.info.source;
      const std::filesystem::path sidecar_path = source.parent_path() / (source.stem().string() + ".tiledata.json");
      auto res = corundum::core::write_json(sidecar_path, sc);
      if (!res)
        return std::unexpected(res.error());
    }

    state.dirty = false;
    return {};
  }

  std::filesystem::path portals_path(const std::filesystem::path &map_path) {
    return std::filesystem::path("data/portals") / map_path.filename();
  }

  std::expected<void, std::string> load_portals(EditorState &state) {
    // load_portals returns {} (empty vector) when the file is absent — not an error.
    auto result = corundum::gameplay::world::load_portals(portals_path(state.map_path));
    if (!result)
      return std::unexpected(result.error());
    state.portals.clear();
    for (const auto &p : *result) {
      PortalEntry e;
      e.col = static_cast<int>(p.col);
      e.row = static_cast<int>(p.row);
      e.w = static_cast<int>(p.w);
      e.h = static_cast<int>(p.h);
      e.target_map = p.target_map;
      e.spawn_col = p.spawn_col;
      e.spawn_row = p.spawn_row;
      state.portals.push_back(std::move(e));
    }
    return {};
  }

} // namespace tools::tilemap
