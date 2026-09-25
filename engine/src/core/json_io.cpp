// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <corundum/core/json_io.hpp>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <system_error>
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
    const ordered_json sorted = sort_keys(j);
    // Write beside the target and rename over it, so a crash or full disk mid-write leaves the
    // previous file intact instead of truncated.
    std::filesystem::path temp_path{path};
    temp_path += ".tmp";

    std::ofstream f(temp_path);
    if (!f)
      return std::unexpected(std::format("cannot open for writing: {}", temp_path.string()));
    f << sorted.dump(2) << '\n';
    f.close();
    std::error_code ignored;
    if (!f) {
      std::filesystem::remove(temp_path, ignored);
      return std::unexpected(std::format("write failed: {}", path.string()));
    }

    std::error_code rename_error;
    std::filesystem::rename(temp_path, path, rename_error);
    if (rename_error) {
      std::filesystem::remove(temp_path, ignored);
      return std::unexpected(std::format("cannot replace {}: {}", path.string(), rename_error.message()));
    }
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
