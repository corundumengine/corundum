#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>
#include <corundum/physics/collision.hpp>
#include <corundum/physics/sys/physics_sys.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace corundum::physics::sys {

  void integrate(corundum::gameplay::component::TransformTable &transforms, float dt) noexcept {
    [[assume(transforms.count <= std::remove_reference_t<decltype(transforms)>::k_max)]];
    float *px = std::assume_aligned<16>(transforms.x.data());
    float *py = std::assume_aligned<16>(transforms.y.data());
    const float *pvx = std::assume_aligned<16>(transforms.dx.data());
    const float *pvy = std::assume_aligned<16>(transforms.dy.data());
    for (uint16_t i = 0; i < transforms.count; ++i) {
      px[i] += pvx[i] * dt;
      py[i] += pvy[i] * dt;
    }
  }

  void apply_input(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                   float speed) noexcept {
    if (!transforms.has(player)) [[unlikely]]
      return;

    const auto slot = transforms.dense_idx(player);
    transforms.dx[slot] = 0.f;
    transforms.dy[slot] = 0.f;

    float vx = 0.f;
    float vy = 0.f;
    if (input.is_held(corundum::input::Action::MoveUp))
      vy = -1.f;
    if (input.is_held(corundum::input::Action::MoveDown))
      vy = 1.f;
    if (input.is_held(corundum::input::Action::MoveLeft))
      vx = -1.f;
    if (input.is_held(corundum::input::Action::MoveRight))
      vx = 1.f;

    const float len_sq = vx * vx + vy * vy;
    if (len_sq > 0.f) {
      const float inv_len = speed / std::sqrt(len_sq);
      transforms.dx[slot] = vx * inv_len;
      transforms.dy[slot] = vy * inv_len;
    }
  }

  void update_player(corundum::gameplay::component::TransformTable &transforms,
                     const corundum::gameplay::component::CollisionTable &collisions,
                     corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                     float player_speed, const corundum::gameplay::world::MapView &map,
                     corundum::gameplay::world::Scene &scene, float dt) noexcept {
    using corundum::gameplay::component::Position;
    using corundum::gameplay::entity::EntityId;

    const auto p_slot = transforms.dense_idx(player);
    const float prev_x = transforms.x[p_slot];
    const float prev_y = transforms.y[p_slot];

    apply_input(transforms, player, input, player_speed);
    integrate(transforms, dt);

    const float map_w = map.world_w_px;
    const float map_h = map.world_h_px;

    const auto &player_rect = collisions.get_rect(player);

    Position p{transforms.x[p_slot], transforms.y[p_slot]};
    const Position prev_pos{prev_x, prev_y};

    if (map.half_tw > 0.f && map.half_th > 0.f) {
      const float scale_ratio = map.scale_ratio();
      const float feet_y = p.y + player_rect.yo * scale_ratio;
      const float prev_feet_y = prev_pos.y + player_rect.yo * scale_ratio;
      auto p_cart = corundum::core::math::iso_to_cart({p.x, feet_y}, map.half_tw, map.half_th, map.x_origin);
      auto prev_cart =
          corundum::core::math::iso_to_cart({prev_pos.x, prev_feet_y}, map.half_tw, map.half_th, map.x_origin);
      Position pc{p_cart.x, p_cart.y};
      const Position pcp{prev_cart.x, prev_cart.y};
      resolve_collisions(pc, pcp, player_rect.w, player_rect.h, map.collisions, 0.f);
      resolve_triangle_collisions(pc, pcp, player_rect.w, player_rect.h, map.collision_triangles, 0.f);
      const auto p_iso = corundum::core::math::cart_to_iso({pc.x, pc.y}, map.half_tw, map.half_th, map.x_origin);
      p.x = p_iso.x;
      p.y = p_iso.y - player_rect.yo * scale_ratio;
    }

    std::array<float, corundum::gameplay::entity::k_max_entities> npc_xs{}, npc_ys{}, npc_ws{}, npc_hs{};
    uint16_t npc_count = 0;
    for (uint16_t i = 0; i < collisions.count; ++i) {
      const EntityId eid = collisions.entities[i];
      if (eid == player)
        continue;
      if (!transforms.has(eid))
        continue;
      const auto &rect = collisions.rects[i];
      const auto np_slot = transforms.dense_idx(eid);
      npc_xs[npc_count] = transforms.x[np_slot];
      npc_ys[npc_count] = transforms.y[np_slot];
      npc_ws[npc_count] = rect.w;
      npc_hs[npc_count] = rect.h;
      ++npc_count;
    }
    const corundum::gameplay::world::tilemap::CollisionRectsView npc_view{
        std::span{npc_xs.data(), npc_count}, std::span{npc_ys.data(), npc_count}, std::span{npc_ws.data(), npc_count},
        std::span{npc_hs.data(), npc_count}};
    resolve_collisions(p, prev_pos, player_rect.w, player_rect.h, npc_view, 0.f);

    p.x = std::clamp(p.x, 0.f, std::max(0.f, map_w - player_rect.w));
    p.y = std::clamp(p.y, 0.f, std::max(0.f, map_h - player_rect.h));

    transforms.x[p_slot] = p.x;
    transforms.y[p_slot] = p.y;

    if (map.half_tw > 0.f && map.half_th > 0.f && !map.portals.empty()) {
      const float portal_feet_y = p.y + player_rect.yo * map.scale_ratio();
      const auto p_cart =
          corundum::core::math::iso_to_cart({p.x, portal_feet_y}, map.half_tw, map.half_th, map.x_origin);
      const float px0 = p_cart.x;
      const float py0 = p_cart.y;
      const float px1 = p_cart.x + player_rect.w;
      const float py1 = p_cart.y + player_rect.h;
      for (const auto &portal : map.portals) {
        if (px1 > portal.x && px0 < portal.x + portal.w && py1 > portal.y && py0 < portal.y + portal.h) {
          scene.pending_transition = {portal.target_map, portal.spawn_col, portal.spawn_row};
          return;
        }
      }
    }
  }

} // namespace corundum::physics::sys
