#include <algorithm>
#include <corundum/core/json_io.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace corundum::core {

  namespace {

    using nlohmann::ordered_json;

    /// Rebuild a json value as an ordered_json with keys in sorted order.
    /// Recursive — handles objects, arrays, and leaf values.
    ordered_json sort_keys(const nlohmann::json &j) { // NOLINT(misc-no-recursion)
      switch (j.type()) {
        case nlohmann::json::value_t::object: {
          ordered_json result = ordered_json::object();
          // Collect keys, sort them, then insert in sorted order.
          std::vector<std::string> keys;
          keys.reserve(j.size());
          for (const auto &[k, _] : j.items())
            keys.push_back(k);
          std::ranges::sort(keys);
          for (const auto &k : keys)
            result[k] = sort_keys(j[k]);
          return result;
        }
        case nlohmann::json::value_t::array: {
          ordered_json result = ordered_json::array();
          for (const auto &elem : j)
            result.push_back(sort_keys(elem));
          return result;
        }
        default:
          // Primitives (string, number, bool, null) — pass through.
          return ordered_json(j); // NOLINT(modernize-return-braced-init-list)
      }
    }

  } // namespace

  std::expected<void, std::string> write_json(const std::filesystem::path &path, const nlohmann::json &j) {
    const auto sorted = sort_keys(j);
    std::ofstream f(path);
    if (!f)
      return std::unexpected(std::format("cannot open for writing: {}", path.string()));
    f << sorted.dump(2) << '\n';
    if (!f)
      return std::unexpected(std::format("write failed: {}", path.string()));
    return {};
  }

  std::expected<nlohmann::json, std::string> read_json(const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f)
      return std::unexpected(std::format("cannot open: {}", path.string()));
    try {
      return nlohmann::json::parse(f, nullptr, true, true);
    } catch (const nlohmann::json::exception &e) {
      return std::unexpected(std::format("malformed JSON in {}: {}", path.string(), e.what()));
    }
  }

} // namespace corundum::core
