// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/codex/loader.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>

#include "core/warn_log.hpp"

#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::codex {

  namespace {

    std::expected<int, std::string> migrate_codex_json(json & /*root*/, int from_version,
                                                       const std::string & /*path*/) {
      return from_version;
    }

    CodexEntry parse_entry(const json &element) {
      CodexEntry entry;
      entry.id = element["id"].get<std::string>();
      entry.title = element["title"].get<std::string>();
      if (element.contains("category"))
        entry.category = element["category"].get<std::string>();
      if (element.contains("body"))
        entry.body = element["body"].get<std::string>();
      return entry;
    }

  } // namespace

  std::expected<std::vector<CodexEntry>, std::string> load_codex_file(const std::filesystem::path &path) {
    const std::string path_string = path.string();

    auto root_result = core::read_json(path, "codex JSON");
    if (!root_result)
      return std::unexpected(std::move(root_result).error());
    json root = std::move(*root_result);

    if (auto prepared =
            core::prepare_schema_version(root, k_codex_schema_version, "Codex", path_string, migrate_codex_json);
        !prepared)
      return std::unexpected(std::move(prepared).error());

    if (auto validated = core::schema_catalog().codex_schema().validate(root); !validated)
      return std::unexpected(std::format("[schema] {}: {}", path_string, validated.error()));

    std::vector<CodexEntry> entries;
    entries.reserve(root["entries"].size());
    for (const auto &element : root["entries"])
      entries.push_back(parse_entry(element));
    return entries;
  }

} // namespace corundum::codex
