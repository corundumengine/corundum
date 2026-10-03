// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/codex/loader.hpp>
#include <corundum/gameplay/codex/registry.hpp>

#include <corundum/core/files.hpp>

#include "core/warn_log.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::gameplay::codex {

  int Registry::load_all(const std::filesystem::path &dir) {
    const auto entries = core::list_dir_entries(dir, {.extensions = {"json"}});
    if (!entries) {
      corundum::detail::warn_log("[codex] cannot read codex directory '{}': {}", dir.string(), entries.error());
      return 0;
    }

    int loaded = 0;
    for (const auto &entry : *entries) {
      if (entry.is_dir)
        continue;

      auto result = load_codex_file(entry.path);
      if (!result) {
        corundum::detail::warn_log("[codex] skipping '{}': {}", entry.name, result.error());
        continue;
      }

      for (auto &codex_entry : *result) {
        const std::string id = codex_entry.id;
        if (entries_.contains(id)) {
          corundum::detail::warn_log("[codex] duplicate entry id '{}' — '{}' is shadowed", id, entry.name);
        } else {
          entries_.emplace(id, std::move(codex_entry));
          ++loaded;
        }
      }
    }

    return loaded;
  }

  const CodexEntry *Registry::find(std::string_view id) const {
    const auto it = entries_.find(id);
    return it != entries_.end() ? &it->second : nullptr;
  }

} // namespace corundum::gameplay::codex
