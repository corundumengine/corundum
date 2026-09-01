#pragma once
#include <corundum/engine.hpp>
#include <corundum/engine_factory.hpp>
#include <corundum/dialogue/action.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/tilemap/tilemap.hpp>

// Deliberate game-facing surface; replacing today's accidental transitive-include
// reliance on engine.hpp's include block. engine.hpp's includes stay (each one
// supplies a complete type for an aggregate member); do not slim them.

namespace corundum {
  using dialogue::EventAction;
  using corundum::world::FlagStore;
  using corundum::world::set_flag;
  using corundum::world::has_flag;
  using corundum::world::clear_flag;
  using corundum::world::visit_count;
  using world::tilemap::Tilemap;
} // namespace corundum
