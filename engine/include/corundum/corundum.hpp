// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <corundum/dialogue/action.hpp> // dialogue::EventAction
#include <corundum/engine.hpp>          // IWYU pragma: export
#include <corundum/engine_factory.hpp>  // IWYU pragma: export
#include <corundum/world/flags.hpp>     // world::set_flag

namespace corundum {

  /** @brief Canonical include for game code: the primary Engine API plus the
   *  nested-namespace names game code names directly.
   *
   * engine.hpp and engine_factory.hpp supply Engine, EngineOptions,
   * parse_engine_args and make_engine and are re-exported intact — the
   * `IWYU pragma: export` markers declare them part of this surface so include
   * analysis does not treat them as unused. The using-declarations below lift the
   * remaining nested-namespace names into corundum::; add a name only when a
   * caller exists.
   */
  using dialogue::EventAction;
  using world::set_flag;
} // namespace corundum
