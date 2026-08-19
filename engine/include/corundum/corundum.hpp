#pragma once
#include <corundum/engine.hpp>
#include <corundum/engine_factory.hpp>
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/flags.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>

// Deliberate game-facing surface; replacing today's accidental transitive-include
// reliance on engine.hpp's include block. engine.hpp's includes stay (each one
// supplies a complete type for an aggregate member); do not slim them.

namespace corundum {
  using gameplay::dialogue::EventAction;
  using gameplay::FlagStore;
  using gameplay::set_flag;
  using gameplay::has_flag;
  using gameplay::clear_flag;
  using gameplay::visit_count;
  using gameplay::world::tilemap::Tilemap;
} // namespace corundum
