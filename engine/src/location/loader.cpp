// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/location/loader.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::location {

  namespace {

    std::expected<int, std::string> migrate_location_json(json & /*root*/, int from_version,
                                                          const std::string & /*path*/) {
      return from_version;
    }

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

  } // namespace

  std::expected<std::vector<Location>, std::string> load_location_file(const std::filesystem::path &path) {
    const std::string path_string = path.string();

    auto root_result = core::read_json(path, "location JSON");
    if (!root_result)
      return std::unexpected(std::move(root_result).error());
    json root = std::move(*root_result);

    if (auto prepared = core::prepare_schema_version(root, k_location_schema_version, "Location", path_string,
                                                     migrate_location_json);
        !prepared)
      return std::unexpected(std::move(prepared).error());

    if (auto validated = core::schema_catalog().location_schema().validate(root); !validated)
      return std::unexpected(std::format("[schema] {}: {}", path_string, validated.error()));

    std::vector<Location> locations;
    locations.reserve(root["locations"].size());
    for (const auto &element : root["locations"])
      locations.push_back(parse_location(element));
    return locations;
  }

} // namespace corundum::location
