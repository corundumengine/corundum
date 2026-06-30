#include <algorithm>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace corundum::core {

  /// Rebuild a json value as an ordered_json with keys in sorted order.
  /// Recursive — handles objects, arrays, and leaf values.
  static nlohmann::ordered_json sort_keys(const nlohmann::json &j) {
    using ordered_json = nlohmann::ordered_json;
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
      return ordered_json(j);
    }
  }

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

} // namespace corundum::core
