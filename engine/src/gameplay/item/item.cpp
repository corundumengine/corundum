#include <corundum/core/json_schema.hpp>
#include <corundum/gameplay/item/loader.hpp>
#include <corundum/gameplay/item/registry.hpp>

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <print>

using json = nlohmann::json;

namespace corundum::gameplay::item {

  namespace {

    struct LoadError : std::runtime_error {
      using std::runtime_error::runtime_error;
    };

    static Item load_item_impl(const std::string &path) {
      std::ifstream f(path);
      if (!f)
        throw LoadError(std::format("cannot open item file: {}", path));

      const json root = [&] {
        try {
          return json::parse(f, nullptr, true, true);
        } catch (const json::exception &e) {
          throw LoadError(std::format("malformed item JSON in {}: {}", path, e.what()));
        }
      }();

      // ── Schema validation ──────────────────────────────────────────────────
      {
        auto sv = core::item_schema().validate(root);
        if (!sv)
          throw LoadError(std::format("[schema] {}: {}", path, sv.error()));
      }

      // Schema guarantees: name is present and non-empty; id is non-empty when present.
      Item item;
      if (root.contains("id") && root["id"].is_string())
        item.id = root["id"].get<std::string>();
      else
        item.id = std::filesystem::path(path).stem().string();

      item.name = root["name"].get<std::string>();

      if (root.contains("description"))
        item.description = root["description"].get<std::string>();

      if (root.contains("icon"))
        item.icon = root["icon"].get<std::string>();

      return item;
    }

  } // namespace

  std::expected<Item, std::string> load_item(const std::filesystem::path &path) {
    try {
      return load_item_impl(path.string());
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

  int Registry::load_all(const std::filesystem::path &dir) {
    int loaded = 0;

    if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
      std::println("[item] no item directory at '{}'", dir.string());
      return loaded;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir)) {
      if (entry.path().extension() != ".json")
        continue;

      auto result = load_item(entry.path());
      if (!result) {
        std::println(stderr, "[item] skipping '{}': {}", entry.path().filename().string(), result.error());
        continue;
      }

      const std::string id = result->id;
      if (items_.contains(id))
        std::println(stderr, "[item] duplicate item id '{}' — '{}' is shadowed", id, entry.path().filename().string());
      else {
        items_.emplace(id, std::move(*result));
        ++loaded;
      }
    }

    return loaded;
  }

  const Item *Registry::find(std::string_view id) const {
    const auto it = items_.find(id);
    return it != items_.end() ? &it->second : nullptr;
  }

} // namespace corundum::gameplay::item
