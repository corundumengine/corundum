// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <corundum/core/content_source.hpp>
#include <corundum/core/json_io.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <span>
#include <string>
#include <string_view>
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

    /// Parse any contiguous range of 1-byte values, mapping nlohmann's exception to the shared message.
    template <typename Range>
    std::expected<nlohmann::json, std::string> parse_range(const Range &range, std::string_view source,
                                                           std::string_view label) {
      try {
        return nlohmann::json::parse(range.begin(), range.end(), nullptr, true, true);
      } catch (const nlohmann::json::exception &e) {
        const std::string_view kind = label.empty() ? "JSON" : label;
        return std::unexpected(std::format("malformed {} in {}: {}", kind, source, e.what()));
      }
    }

  } // namespace

  std::string format_json(const nlohmann::json &j) {
    return sort_keys(j).dump(2) + '\n';
  }

  std::expected<void, std::string> write_file_atomic(const std::filesystem::path &path, std::string_view contents) {
    // Write beside the target and rename over it, so a crash or full disk mid-write leaves the
    // previous file intact instead of truncated.
    std::filesystem::path temp_path{path};
    temp_path += ".tmp";

    std::ofstream f(temp_path);
    if (!f)
      return std::unexpected(std::format("cannot open for writing: {}", temp_path.string()));
    f << contents;
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

  std::expected<void, std::string> write_json(const std::filesystem::path &path, const nlohmann::json &j) {
    return write_file_atomic(path, format_json(j));
  }

  std::expected<nlohmann::json, std::string> parse_json(std::string_view text, std::string_view source,
                                                        std::string_view label) {
    return parse_range(text, source, label);
  }

  std::expected<nlohmann::json, std::string> parse_json(std::span<const std::uint8_t> bytes, std::string_view source,
                                                        std::string_view label) {
    return parse_range(bytes, source, label);
  }

  std::expected<nlohmann::json, std::string> read_json(const ContentSource &content, const std::filesystem::path &path,
                                                       std::string_view label) {
    const std::expected<std::vector<std::uint8_t>, std::string> bytes = content.read(path);
    if (!bytes) {
      // Keep the adapter's message: it already names the path and says why (missing vs. read failure).
      return std::unexpected(label.empty() ? bytes.error() : std::format("{}: {}", label, bytes.error()));
    }
    return parse_json(*bytes, path.string(), label);
  }

} // namespace corundum::core
