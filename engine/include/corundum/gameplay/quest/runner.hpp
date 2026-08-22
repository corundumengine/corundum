#pragma once

#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/quest/registry.hpp>
#include <corundum/gameplay/quest/system.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace corundum::gameplay::quest {

  /** @brief Coordinates quest lookup and progression against a FlagStore.
   *
   * Wraps the free functions in quest/system.hpp so a caller cannot split a
   * Registry::find() from the start()/advance() call that must follow it —
   * an unknown quest id becomes a returned error instead of a call site that
   * forgot to check for nullptr.
   */
  class Runner {
  public:
    Runner(const Registry &registry, FlagStore &flags) noexcept : registry_(registry), flags_(flags) {}

    /** @brief Start a quest by id.
     *  @return ok on success, or an error if @p quest_id is not in the registry. */
    [[nodiscard]] std::expected<void, std::string> start(std::string_view quest_id);

    /** @brief Advance a quest by id to a named stage.
     *  @return ok on success, or an error if @p quest_id is not in the registry. */
    [[nodiscard]] std::expected<void, std::string> advance(std::string_view quest_id, std::string_view stage_name);

  private:
    const Registry &registry_;
    FlagStore &flags_;
  };

} // namespace corundum::gameplay::quest
