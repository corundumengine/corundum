// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/credits/loader.hpp>

#include <corundum/core/json_io.hpp>
#include <corundum/gameplay/credits/credits.hpp>

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::gameplay::credits {

  namespace {

    std::expected<CreditsSection, std::string> parse_section(const json &element, std::size_t index) {
      if (!element.is_object())
        return std::unexpected(std::format("credits section {} must be an object", index));

      CreditsSection section;
      if (element.contains("heading")) {
        if (!element.at("heading").is_string())
          return std::unexpected(std::format("credits section {} heading must be a string", index));
        section.heading = element.at("heading").get<std::string>();
      }
      if (element.contains("lines")) {
        const json &lines = element.at("lines");
        if (!lines.is_array())
          return std::unexpected(std::format("credits section {} lines must be an array", index));
        for (const json &line : lines) {
          if (!line.is_string())
            return std::unexpected(std::format("credits section {} lines must be strings", index));
          section.lines.push_back(line.get<std::string>());
        }
      }
      return section;
    }

  } // namespace

  std::expected<std::vector<CreditsSection>, std::string> load_credits_file(const std::filesystem::path &path) {
    std::expected<json, std::string> root_result = core::read_json(path, "credits JSON");
    if (!root_result)
      return std::unexpected(std::move(root_result).error());

    const json &root = *root_result;
    if (!root.is_object())
      return std::unexpected(std::format("credits JSON root must be an object: {}", path.string()));
    if (!root.contains("sections") || !root.at("sections").is_array())
      return std::unexpected(std::format("credits JSON needs a 'sections' array: {}", path.string()));

    std::vector<CreditsSection> sections;
    sections.reserve(root.at("sections").size());
    std::size_t index = 0;
    for (const json &element : root.at("sections")) {
      std::expected<CreditsSection, std::string> section = parse_section(element, index);
      if (!section)
        return std::unexpected(std::move(section).error());
      sections.push_back(std::move(*section));
      ++index;
    }
    return sections;
  }

} // namespace corundum::gameplay::credits
