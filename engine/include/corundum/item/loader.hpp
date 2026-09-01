#pragma once

#include <corundum/item/item.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::item {

  /**
   * @brief Load and validate an item from a JSON file.
   *
   * Validates the JSON schema and extracts id, name, description, and icon.
   *
   * @param path Filesystem path to the item JSON file.
   * @return The parsed Item on success, or an error message on failure.
   */
  [[nodiscard]] std::expected<Item, std::string> load_item(const std::filesystem::path &path);

} // namespace corundum::item
