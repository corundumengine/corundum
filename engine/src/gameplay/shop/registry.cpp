// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/files.hpp>
#include <corundum/gameplay/shop/loader.hpp>
#include <corundum/gameplay/shop/registry.hpp>

#include "core/warn_log.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::gameplay::shop {

  int Registry::load_all(const std::filesystem::path &dir) {
    const auto entries = core::list_dir_entries(dir, {.extensions = {"json"}});
    if (!entries) {
      corundum::detail::warn_log("[shop] cannot read shop directory '{}': {}", dir.string(), entries.error());
      return 0;
    }

    int loaded = 0;
    for (const auto &entry : *entries) {
      if (entry.is_dir)
        continue;

      auto result = load_shop_file(entry.path);
      if (!result) {
        corundum::detail::warn_log("[shop] skipping '{}': {}", entry.name, result.error());
        continue;
      }

      for (auto &shop : *result) {
        const std::string id = shop.id;
        if (shops_.contains(id)) {
          corundum::detail::warn_log("[shop] duplicate shop id '{}' — '{}' is shadowed", id, entry.name);
        } else {
          shops_.emplace(id, std::move(shop));
          ++loaded;
        }
      }
    }

    return loaded;
  }

  const Shop *Registry::find(std::string_view id) const {
    const auto it = shops_.find(id);
    return it != shops_.end() ? &it->second : nullptr;
  }

} // namespace corundum::gameplay::shop
