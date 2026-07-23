#pragma once

#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::gameplay::dialogue {

  /// Sentinel node id that terminates a dialogue without a matching graph node.
  constexpr std::string_view ending_node = "end";

  /// Parses a dialogue JSON file and returns a fully-validated Graph.
  /// All error categories (schema, unknown types, broken edges, bad JSON,
  /// file-not-found) are unified into the error string.
  [[nodiscard]] std::expected<Graph, std::string> load_graph(const std::filesystem::path &path);

} // namespace corundum::gameplay::dialogue
