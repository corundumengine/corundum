#pragma once

#include <corundum/quest/quest.hpp>
#include <expected>
#include <filesystem>
#include <string>

namespace corundum::quest {

  /**
   * @brief Load and validate a quest from a JSON file.
   *
   * Validates the JSON schema, stage uniqueness, sequence ordering,
   * and ensures at least one resolved stage exists.
   *
   * @param path Filesystem path to the quest JSON file.
   * @return The parsed Quest on success, or an error message on failure.
   */
  [[nodiscard]] std::expected<Quest, std::string> load_quest(const std::filesystem::path &path);

} // namespace corundum::quest
