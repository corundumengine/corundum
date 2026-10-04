// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/world/ui_stack.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace corundum {

  struct Engine;

  namespace input {
    struct InputIntent;
  } // namespace input

  namespace platform {
    class Renderer;
  } // namespace platform

  /** @brief Paint-order buckets for self-gated render hooks.
   *
   *  The engine draws them in declaration order: Hud (only while no screen is open), Modal
   *  overlays (the dialogue box), then hub paint. Each hook gates on its own state and, for a
   *  modal overlay, on being the top-of-stack mode, so a screen pushed over it does not
   *  double-draw.
   */
  enum class RenderLayer : std::uint8_t {
    Hud,      ///< Gameplay HUD strip; only while no screen is open and no prompt is showing.
    Modal,    ///< Modal overlays (the dialogue box).
    HubPanel, ///< Hub panels drawn above the tab strip (Codex, Map, Loot, Barter).
    HubStrip, ///< Hub tab strip.
    Count,
  };

  using ScreenUpdateFn = std::function<void(Engine &, const input::InputIntent &)>;

  using ScreenRenderFn = std::function<void(Engine &, platform::Renderer &, core::math::Vec2 viewport)>;

  /** @brief One screen's step and render behaviour, keyed by GameMode.
   *
   *  Gameplay screens registered by gameplay hooks capture the gameplay owner (see the
   *  Runtime member-order contract); engine-owned screens capture nothing and read the
   *  `Engine &` argument.
   */
  struct ScreenSpec {
    /** @brief When true, update() replaces the whole simulation step: world::update, the
     *  fixed_step_systems and on_fixed_update all skip. When false the world step runs
     *  normally and the screen has no update hook — it does not mean "no per-step work"; a
     *  non-step-owning screen does its per-step work from a fixed_step_systems entry, not
     *  from this hook. */
    bool owns_step{false};

    ScreenUpdateFn update{};

    ScreenRenderFn render{};
  };

  /** @brief GameMode → ScreenSpec table plus the ordered layer hooks.
   *
   *  Registration happens once at initialize time; dispatch is an indirect call per step /
   *  frame. The std::function storage is chosen deliberately (registration-time allocation,
   *  no virtual hierarchy) — do not "optimize" it into per-frame allocation or a vtable.
   */
  class ScreenRegistry {
  public:
    /** @brief Register @p spec for @p mode, replacing any existing entry. */
    void add(world::GameMode mode, ScreenSpec spec) {
      for (Entry &entry : screens_) {
        if (entry.mode == mode) {
          entry.spec = std::move(spec);
          return;
        }
      }
      screens_.push_back(Entry{.mode = mode, .spec = std::move(spec)});
    }

    /** @brief The spec registered for @p mode, or nullptr when none is. */
    [[nodiscard]] const ScreenSpec *find(world::GameMode mode) const noexcept {
      for (const Entry &entry : screens_) {
        if (entry.mode == mode)
          return &entry.spec;
      }
      return nullptr;
    }

    /** @brief Append a self-gated hook to @p layer. */
    void add_layer(RenderLayer layer, ScreenRenderFn hook) {
      layers_[static_cast<std::size_t>(layer)].push_back(std::move(hook));
    }

    /** @brief Invoke every hook in @p layer, in registration order. */
    void render_layer(RenderLayer layer, Engine &engine, platform::Renderer &r, core::math::Vec2 viewport) const {
      for (const ScreenRenderFn &hook : layers_[static_cast<std::size_t>(layer)])
        hook(engine, r, viewport);
    }

  private:
    struct Entry {
      world::GameMode mode{};

      ScreenSpec spec{};
    };

    std::vector<Entry> screens_;

    std::array<std::vector<ScreenRenderFn>, static_cast<std::size_t>(RenderLayer::Count)> layers_;
  };

} // namespace corundum
