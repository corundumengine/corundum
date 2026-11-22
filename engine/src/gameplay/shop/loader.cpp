// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/content_source.hpp>
#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/gameplay/shop/loader.hpp>
#include <corundum/gameplay/shop/shop.hpp>

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

namespace corundum::gameplay::shop {

  namespace {

    Shop parse_shop(const json &element) {
      Shop shop;
      shop.id = element["id"].get<std::string>();
      shop.name = element["name"].get<std::string>();
      if (element.contains("stock")) {
        for (const auto &stock : element["stock"]) {
          StockEntry entry;
          entry.item = stock["item"].get<std::string>();
          if (stock.contains("price"))
            entry.price = stock["price"].get<int>();
          shop.stock.push_back(std::move(entry));
        }
      }
      return shop;
    }

    constexpr std::string_view k_shop_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/shop",
  "type": "object",
  "required": ["shops"],
  "properties": {
    "schema_version": { "type": "integer", "minimum": 1 },
    "shops": {
      "type": "array",
      "items": { "$ref": "#/definitions/shop" }
    }
  },
  "definitions": {
    "shop": {
      "type": "object",
      "required": ["id", "name"],
      "properties": {
        "id":       { "type": "string", "minLength": 1 },
        "name":     { "type": "string", "minLength": 1 },
        "stock": {
          "type": "array",
          "items": { "$ref": "#/definitions/stock" }
        }
      }
    },
    "stock": {
      "type": "object",
      "required": ["item"],
      "properties": {
        "item":  { "type": "string", "minLength": 1 },
        "price": { "type": "integer", "minimum": 0 }
      }
    }
  }
})";

  } // namespace

  Loader::Loader() : schema_(core::SchemaValidator::from_embedded(k_shop_schema)) {}

  std::expected<std::vector<Shop>, std::string> Loader::load(const core::ContentSource &content,
                                                             const std::filesystem::path &path) const {
    const std::string path_string = path.string();

    auto root_result = core::read_json(content, path, "shop JSON");
    if (!root_result)
      return std::unexpected(std::move(root_result).error());
    json root = std::move(*root_result);

    if (auto prepared = core::prepare_schema_version(root, k_shop_schema_version, "Shop", path_string, {}); !prepared)
      return std::unexpected(std::move(prepared).error());

    if (auto validated = schema_.validate(root); !validated)
      return std::unexpected(std::format("[schema] {}: {}", path_string, validated.error()));

    std::vector<Shop> shops;
    shops.reserve(root["shops"].size());
    for (const auto &element : root["shops"])
      shops.push_back(parse_shop(element));
    return shops;
  }

} // namespace corundum::gameplay::shop
