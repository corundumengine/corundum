// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_io.hpp>
#include <corundum/core/json_schema.hpp>
#include <corundum/core/schema_version.hpp>
#include <corundum/shop/loader.hpp>

#include <expected>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;

namespace corundum::shop {

  namespace {

    std::expected<int, std::string> migrate_shop_json(json & /*root*/, int from_version, const std::string & /*path*/) {
      return from_version;
    }

    Shop parse_shop(const json &element) {
      Shop shop;
      shop.id = element["id"].get<std::string>();
      shop.name = element["name"].get<std::string>();
      if (element.contains("faction"))
        shop.faction = element["faction"].get<std::string>();
      if (element.contains("buy_rate"))
        shop.buy_rate = element["buy_rate"].get<float>();
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

  } // namespace

  std::expected<std::vector<Shop>, std::string> load_shop_file(const std::filesystem::path &path) {
    const std::string path_string = path.string();

    auto root_result = core::read_json(path, "shop JSON");
    if (!root_result)
      return std::unexpected(std::move(root_result).error());
    json root = std::move(*root_result);

    if (auto prepared =
            core::prepare_schema_version(root, k_shop_schema_version, "Shop", path_string, migrate_shop_json);
        !prepared)
      return std::unexpected(std::move(prepared).error());

    if (auto validated = core::schema_catalog().shop_schema().validate(root); !validated)
      return std::unexpected(std::format("[schema] {}: {}", path_string, validated.error()));

    std::vector<Shop> shops;
    shops.reserve(root["shops"].size());
    for (const auto &element : root["shops"])
      shops.push_back(parse_shop(element));
    return shops;
  }

} // namespace corundum::shop
