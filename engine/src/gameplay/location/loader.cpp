// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/content_source.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/gameplay/location/loader.hpp>
#include <corundum/gameplay/location/location.hpp>

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

namespace corundum::gameplay::location {

namespace {

Location parse_location(const json &element) {
  Location location;
  location.id = element["id"].get<std::string>();
  location.name = element["name"].get<std::string>();
  if (element.contains("zone"))
    location.zone = element["zone"].get<std::string>();
  if (element.contains("map"))
    location.map = element["map"].get<std::string>();
  if (element.contains("col"))
    location.col = element["col"].get<float>();
  if (element.contains("row"))
    location.row = element["row"].get<float>();
  if (element.contains("return_to_world"))
    location.return_to_world = element["return_to_world"].get<bool>();
  return location;
}

constexpr std::string_view k_location_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/location",
  "type": "object",
  "required": ["locations"],
  "properties": {
    "schema_version": { "type": "integer", "minimum": 1 },
    "locations": {
      "type": "array",
      "items": { "$ref": "#/definitions/location" }
    }
  },
  "definitions": {
    "location": {
      "type": "object",
      "required": ["id", "name"],
      "properties": {
        "id":              { "type": "string", "minLength": 1 },
        "name":            { "type": "string", "minLength": 1 },
        "zone":            { "type": "string" },
        "map":             { "type": "string" },
        "col":             { "type": "number", "minimum": 0 },
        "row":             { "type": "number", "minimum": 0 },
        "return_to_world": { "type": "boolean" }
      }
    }
  }
})";

} // namespace

Loader::Loader() : schema_(core::SchemaValidator::from_embedded(k_location_schema)) {}

std::expected<std::vector<Location>, std::string> Loader::load(const core::ContentSource &content,
                                                               const std::filesystem::path &path) const {
  const std::string path_string = path.string();

  auto root_result = core::read_json(content, path, "location JSON");
  if (!root_result)
    return std::unexpected(std::move(root_result).error());
  json root = std::move(*root_result);

  if (auto prepared = core::prepare_schema_version(root, k_location_schema_version, "Location", path_string, {});
      !prepared)
    return std::unexpected(std::move(prepared).error());

  if (auto validated = schema_.validate(root); !validated)
    return std::unexpected(std::format("[schema] {}: {}", path_string, validated.error()));

  std::vector<Location> locations;
  locations.reserve(root["locations"].size());
  for (const auto &element : root["locations"])
    locations.push_back(parse_location(element));
  return locations;
}

} // namespace corundum::gameplay::location
