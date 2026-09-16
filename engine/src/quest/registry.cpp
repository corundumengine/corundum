// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/files.hpp>
#include <corundum/quest/loader.hpp>
#include <corundum/quest/registry.hpp>

#include <cstdio>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::quest {

  int Registry::load_all(const std::filesystem::path &dir) {
    const auto entries = core::list_dir_entries(dir, {.extensions = {"json"}});
    if (!entries) {
      std::println(stderr, "[quest] cannot read quest directory '{}': {}", dir.string(), entries.error());
      return 0;
    }

    int loaded = 0;
    for (const auto &entry : *entries) {
      if (entry.is_dir)
        continue;

      auto result = load_quest(entry.path);
      if (!result) {
        std::println(stderr, "[quest] skipping '{}': {}", entry.name, result.error());
        continue;
      }

      const std::string id = result->quest_id;
      if (quests_.contains(id)) {
        std::println(stderr, "[quest] duplicate quest id '{}' — '{}' is shadowed", id, entry.name);
      } else {
        quests_.emplace(id, std::move(*result));
        ++loaded;
      }
    }

    return loaded;
  }

  const Quest *Registry::find(std::string_view quest_id) const {
    const auto it = quests_.find(quest_id);
    return it != quests_.end() ? &it->second : nullptr;
  }

} // namespace corundum::quest
