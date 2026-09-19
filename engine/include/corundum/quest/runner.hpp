// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/quest/registry.hpp>
#include <corundum/world/flags.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace corundum::quest {

  /** @brief Coordinates quest lookup and progression against a FlagStore.
   *
   * Wraps the free functions in quest/system.hpp so a caller cannot split a
   * Registry::find() from the start()/advance() call that must follow it —
   * an unknown quest id becomes a returned error instead of a call site that
   * forgot to check for nullptr.
   *
   * Error text is phrased for dialogue-event logging (the primary caller), e.g.
   * `quest_start("x") references unknown quest`; treat it as a diagnostic, not
   * as a stable interface for other callers.
   */
  class Runner {
  public:
    Runner(const Registry &registry, corundum::world::FlagStore &flags) noexcept
        : registry_(&registry), flags_(&flags) {}

    /** @brief Start a quest by id.
     *
     *  Starting a quest that is already underway is a no-op, so an ok return
     *  does not by itself mean the quest's flag moved.
     *
     *  @return ok on success, or an error if @p quest_id is not in the registry. */
    [[nodiscard]] std::expected<void, std::string> start(std::string_view quest_id);

    /** @brief Advance a quest by id to a named stage.
     *
     *  A @p stage_name that the quest does not define is a no-op reported to
     *  stderr, so an ok return does not by itself mean the quest moved.
     *
     *  @return ok on success, or an error if @p quest_id is not in the registry. */
    [[nodiscard]] std::expected<void, std::string> advance(std::string_view quest_id, std::string_view stage_name);

  private:
    // Pointers, not references: the project's clang-tidy config rejects reference data members
    // (cppcoreguidelines-avoid-const-or-ref-data-members). Both are non-null by construction.
    const Registry *registry_;
    corundum::world::FlagStore *flags_;
  };

} // namespace corundum::quest
