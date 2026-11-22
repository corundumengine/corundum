// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/codex/loader.hpp>

#include <corundum/core/content_source.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/gameplay/codex/codex.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::gameplay::codex {

  namespace {

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

    constexpr std::string_view k_codex_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/codex",
  "type": "object",
  "required": ["entries"],
  "properties": {
    "schema_version": { "type": "integer", "minimum": 1 },
    "entries": {
      "type": "array",
      "items": { "$ref": "#/definitions/entry" }
    }
  },
  "definitions": {
    "entry": {
      "type": "object",
      "required": ["id", "title"],
      "properties": {
        "id":       { "type": "string", "minLength": 1 },
        "title":    { "type": "string", "minLength": 1 },
        "category": { "type": "string" },
        "body":     { "type": "string" }
      }
    }
  }
})";

  } // namespace

  Loader::Loader() : schema_(core::SchemaValidator::from_embedded(k_codex_schema)) {}

  std::expected<std::vector<CodexEntry>, std::string> Loader::load(const core::ContentSource &content,
                                                                   const std::filesystem::path &path) const {
    const std::string path_string = path.string();

    auto root_result = core::read_json(content, path, "codex JSON");
    if (!root_result)
      return std::unexpected(std::move(root_result).error());
    json root = std::move(*root_result);

    if (auto prepared = core::prepare_schema_version(root, k_codex_schema_version, "Codex", path_string, {}); !prepared)
      return std::unexpected(std::move(prepared).error());

    if (auto validated = schema_.validate(root); !validated)
      return std::unexpected(std::format("[schema] {}: {}", path_string, validated.error()));

    std::vector<CodexEntry> entries;
    entries.reserve(root["entries"].size());
    for (const auto &element : root["entries"])
      entries.push_back(parse_entry(element));
    return entries;
  }

} // namespace corundum::gameplay::codex
