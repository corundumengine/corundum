// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "save.hpp"
#include "editor_state.hpp"
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/serialize.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

#include <cstdio>
#include <expected>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>
#include <string>
#include <utility>
#include <vector>

namespace tools::tilesmith {

  std::expected<void, std::string> save_tilemap(EditorState &state) {
    // 1. Read existing JSON for base-merge (preserves unknown keys), or start from an empty
    // object if this path doesn't exist yet (Save As to a new filename).
    nlohmann::json base = nlohmann::json::object();
    if (std::filesystem::exists(state.map_path)) {
      std::ifstream in(state.map_path);
      if (!in)
        return std::unexpected("Cannot open: " + state.map_path.string());
      base = nlohmann::json::parse(in, nullptr, true, true);
    }

    // 2. Serialize tilemap onto base
    const nlohmann::json j = corundum::world::tilemap::serialize(state.map, &base);

    // 3. Write tilemap
    {
      auto res = corundum::core::write_json(state.map_path, j);
      if (!res)
        return std::unexpected(res.error());
    }

    // 4. Save portals via engine serializer
    {
      const nlohmann::json portals_json = corundum::world::serialize(state.portals);
      const auto ppath = portals_path(state.map_path);
      std::filesystem::create_directories(ppath.parent_path());
      auto res = corundum::core::write_json(ppath, portals_json);
      if (!res)
        return std::unexpected(res.error());
    }

    state.dirty = false;
    return {};
  }

  std::filesystem::path portals_path(const std::filesystem::path &map_path) {
    return std::filesystem::path("data/portals") / map_path.filename();
  }

  void try_save(EditorState &state) {
    state.validation_errors = corundum::world::tilemap::validate(state.map);
    if (!state.validation_errors.empty()) {
      state.show_validation_popup = true;
      return;
    }
    if (auto r = save_tilemap(state); r) {
      std::println("[Tilesmith] Saved: {}", state.map_path.string());
    } else {
      state.last_io_error = r.error();
      state.show_io_error_popup = true;
      std::println(stderr, "[Tilesmith] Save failed: {}", r.error());
    }
  }

  std::expected<void, std::string> load_portals(EditorState &state) {
    // load_portals returns {} (empty vector) when the file is absent — not an error.
    auto result = corundum::world::load_portals(portals_path(state.map_path));
    if (!result)
      return std::unexpected(result.error());
    state.portals = std::move(*result);
    return {};
  }

} // namespace tools::tilesmith
