// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/files.hpp>
#include <corundum/dialogue/loader.hpp>
#include <corundum/dialogue/registry.hpp>

#include <cstdio>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::dialogue {

  int Registry::load_all(const std::filesystem::path &dir) {
    const auto entries = corundum::core::list_dir_entries(dir, {.extensions = {"json"}});
    if (!entries) {
      std::println(stderr, "[dialogue] cannot read dialogue directory '{}': {}", dir.string(), entries.error());
      return 0;
    }

    int loaded = 0;
    for (const auto &entry : *entries) {
      if (entry.is_dir)
        continue;

      auto result = load_graph(entry.path);
      if (!result) {
        std::println(stderr, "[dialogue] skipping '{}': {}", entry.name, result.error());
        continue;
      }

      const std::string id = result->graph_id;
      if (graphs_.contains(id)) {
        std::println(stderr, "[dialogue] duplicate graph id '{}' — '{}' is shadowed", id, entry.name);
      } else {
        graphs_.emplace(id, std::move(*result));
        ++loaded;
      }
    }

    return loaded;
  }

  const Graph *Registry::find(std::string_view graph_id) const {
    const auto it = graphs_.find(graph_id);
    return it != graphs_.end() ? &it->second : nullptr;
  }

} // namespace corundum::dialogue
