// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/files.hpp>
#include <corundum/location/loader.hpp>
#include <corundum/location/registry.hpp>

#include "core/warn_log.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::location {

  int Registry::load_all(const std::filesystem::path &dir) {
    const auto entries = core::list_dir_entries(dir, {.extensions = {"json"}});
    if (!entries) {
      corundum::detail::warn_log("[location] cannot read location directory '{}': {}", dir.string(), entries.error());
      return 0;
    }

    int loaded = 0;
    for (const auto &entry : *entries) {
      if (entry.is_dir)
        continue;

      auto result = load_location_file(entry.path);
      if (!result) {
        corundum::detail::warn_log("[location] skipping '{}': {}", entry.name, result.error());
        continue;
      }

      for (auto &location : *result) {
        const std::string id = location.id;
        if (locations_.contains(id)) {
          corundum::detail::warn_log("[location] duplicate location id '{}' — '{}' is shadowed", id, entry.name);
        } else {
          locations_.emplace(id, std::move(location));
          ++loaded;
        }
      }
    }

    return loaded;
  }

  const Location *Registry::find(std::string_view id) const {
    const auto it = locations_.find(id);
    return it != locations_.end() ? &it->second : nullptr;
  }

} // namespace corundum::location
