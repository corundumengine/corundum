// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/platform/null/null_renderer.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/world/ui_stack.hpp>

#include <string>
#include <vector>

namespace {

  using corundum::RenderLayer;
  using corundum::ScreenRegistry;
  using corundum::ScreenSpec;

} // namespace

TEST_CASE("ScreenRegistry: a registered spec is found and defaults to not owning the step") {
  ScreenRegistry registry;
  CHECK(registry.find(corundum::world::GameMode::Menu) == nullptr);

  registry.add(corundum::world::GameMode::Menu, ScreenSpec{});
  const ScreenSpec *found = registry.find(corundum::world::GameMode::Menu);
  REQUIRE(found != nullptr);
  CHECK_FALSE(found->owns_step);
  CHECK_FALSE(static_cast<bool>(found->update));
  CHECK_FALSE(static_cast<bool>(found->render));

  // A later add for the same mode replaces the spec rather than duplicating it.
  registry.add(corundum::world::GameMode::Menu, ScreenSpec{.owns_step = true});
  const ScreenSpec *replaced = registry.find(corundum::world::GameMode::Menu);
  REQUIRE(replaced != nullptr);
  CHECK(replaced->owns_step);
}

TEST_CASE("ScreenRegistry: render_layer invokes only that layer, in registration order") {
  ScreenRegistry registry;
  std::vector<std::string> calls;

  registry.add_layer(RenderLayer::Modal, [&calls](corundum::Engine &, corundum::platform::Renderer &,
                                                  corundum::core::math::Vec2) { calls.emplace_back("modal-1"); });
  registry.add_layer(RenderLayer::HubStrip, [&calls](corundum::Engine &, corundum::platform::Renderer &,
                                                     corundum::core::math::Vec2) { calls.emplace_back("strip"); });
  registry.add_layer(RenderLayer::Modal, [&calls](corundum::Engine &, corundum::platform::Renderer &,
                                                  corundum::core::math::Vec2) { calls.emplace_back("modal-2"); });

  corundum::Engine engine;
  corundum::platform::null::NullRenderer renderer;
  registry.render_layer(RenderLayer::Modal, engine, renderer, {.x = 1.f, .y = 2.f});

  REQUIRE(calls.size() == 2);
  CHECK(calls[0] == "modal-1");
  CHECK(calls[1] == "modal-2");
}

TEST_CASE("ScreenRegistry: an unregistered mode has no spec") {
  ScreenRegistry registry;
  registry.add(corundum::gameplay::screens::Inventory, ScreenSpec{.owns_step = true});
  CHECK(registry.find(corundum::gameplay::screens::Journal) == nullptr);
  CHECK(registry.find(corundum::gameplay::screens::Inventory) != nullptr);
}
