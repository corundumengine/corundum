// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/entities/components.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/animation_table.hpp>
#include <corundum/entities/tables/collision_table.hpp>
#include <corundum/entities/tables/transform_table.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/sprites/sprite.hpp>
#include <cstdint>
#include <doctest/doctest.h>
#include <optional>

using namespace corundum::entities;
using corundum::entities::Position;
using corundum::entities::Sprite;
using corundum::entities::Velocity;

namespace {

  /// A one-frame sprite using the default animation, pointing at @p sprite_id. Cases that need a
  /// Sprite but don't care which art it names use this instead of repeating its three fields.
  Sprite default_sprite(corundum::sprites::SpriteId sprite_id) {
    return Sprite{.sprite_id = sprite_id, .anim_id = corundum::sprites::AnimId::Default, .frame_index = 0};
  }

  /// Spawn with the standard components, requiring success. The pool-exhaustion case exercises the
  /// failure path directly, so every other case here can assume a free slot.
  EntityId spawn_or_require(World &w, Position pos, Velocity vel, Sprite spr) {
    const std::optional<EntityId> id = spawn(w, pos, vel, spr);
    REQUIRE(id.has_value());
    return id.value_or(EntityId::invalid());
  }

  EntityId spawn_or_require(World &w, Position pos, Velocity vel, Sprite spr, Animation anim) {
    const std::optional<EntityId> id = spawn(w, pos, vel, spr, anim);
    REQUIRE(id.has_value());
    return id.value_or(EntityId::invalid());
  }

} // namespace

TEST_CASE("EntityId default is invalid") {
  const EntityId e{};
  CHECK_FALSE(e.valid());
  CHECK(e == EntityId::invalid());
}

TEST_CASE("EntityId equality") {
  CHECK(EntityId{1, 2} == EntityId{1, 2});
  CHECK(EntityId{1, 2} != EntityId{1, 3});
}

TEST_CASE("EntityManager create returns valid handle") {
  EntityManager mgr;
  const EntityId e = mgr.create();
  CHECK(e.valid());
  CHECK(mgr.is_live(e));
  CHECK(mgr.alive() == 1);
}

TEST_CASE("EntityManager destroy invalidates handle") {
  EntityManager mgr;
  const EntityId e = mgr.create();
  mgr.destroy(e);
  CHECK_FALSE(mgr.is_live(e));
  CHECK(mgr.alive() == 0);
}

TEST_CASE("EntityManager generation increments on destroy") {
  EntityManager mgr;
  const EntityId e1 = mgr.create();
  const std::uint32_t old_gen = e1.generation;
  mgr.destroy(e1);
  const EntityId e2 = mgr.create();
  CHECK(e2.index == e1.index);
  CHECK(e2.generation != old_gen);
  CHECK_FALSE(mgr.is_live(e1));
  CHECK(mgr.is_live(e2));
}

TEST_CASE("EntityManager double-destroy is safe") {
  EntityManager mgr;
  const EntityId e = mgr.create();
  mgr.destroy(e);
  mgr.destroy(e);
  CHECK(mgr.alive() == 0);
  const EntityId e2 = mgr.create();
  CHECK(mgr.is_live(e2));
}

TEST_CASE("EntityManager stale handle rejected after slot reuse") {
  EntityManager mgr;
  const EntityId e1 = mgr.create();
  mgr.destroy(e1);
  const EntityId e2 = mgr.create();
  const EntityId e3 = mgr.create();
  CHECK_FALSE(mgr.is_live(e1));
  CHECK(mgr.is_live(e2));
  CHECK(mgr.is_live(e3));
  mgr.destroy(e1);
  CHECK(mgr.is_live(e2));
  CHECK(mgr.is_live(e3));
}

TEST_CASE("EntityManager pool exhaustion") {
  EntityManager mgr;
  for (std::uint32_t i = 0; i < k_max_entities; ++i) {
    const EntityId e = mgr.create();
    CHECK(e.valid());
  }
  CHECK(mgr.full());
  CHECK(mgr.alive() == k_max_entities);
}

TEST_CASE("TransformTable insert/remove/has") {
  EntityManager mgr;
  TransformTable table;

  const EntityId e = mgr.create();
  CHECK_FALSE(table.has(e));

  table.insert(e, 1.f, 2.f, 0.1f, 0.2f);
  CHECK(table.has(e));

  table.remove(e);
  CHECK_FALSE(table.has(e));

  mgr.destroy(e);
}

TEST_CASE("TransformTable stale EntityId rejected") {
  EntityManager mgr;
  TransformTable table;

  const EntityId e1 = mgr.create();
  table.insert(e1, 1.f, 2.f, 3.f, 4.f);
  CHECK(table.has(e1));

  table.remove(e1);
  mgr.destroy(e1);

  const EntityId e2 = mgr.create();
  CHECK(e2.index == e1.index);
  CHECK(e2.generation != e1.generation);

  CHECK_FALSE(table.has(e1));

  table.insert(e2, 5.f, 6.f, 7.f, 8.f);
  CHECK(table.has(e2));
  CHECK_FALSE(table.has(e1));

  table.remove(e2);
  mgr.destroy(e2);
}

TEST_CASE("TransformTable try_dense_index returns the slot for a live entity") {
  EntityManager mgr;
  TransformTable table;

  const EntityId e = mgr.create();
  table.insert(e, 1.f, 2.f, 3.f, 4.f);

  const std::optional<std::uint32_t> slot = table.try_dense_index(e);
  REQUIRE(slot.has_value());
  CHECK(slot == table.dense_index(e));

  table.remove(e);
  mgr.destroy(e);
}

TEST_CASE("TransformTable try_dense_index rejects a stale handle after slot reuse") {
  EntityManager mgr;
  TransformTable table;

  const EntityId a = mgr.create();
  table.insert(a, 1.f, 2.f, 0.f, 0.f);
  table.remove(a);
  mgr.destroy(a);

  const EntityId b = mgr.create();
  CHECK(b.index == a.index);
  CHECK(b.generation != a.generation);
  table.insert(b, 5.f, 6.f, 0.f, 0.f);

  CHECK_FALSE(table.try_dense_index(a).has_value());
  CHECK_FALSE(table.has(a));
  CHECK(table.try_dense_index(b).has_value());

  table.remove(b);
  mgr.destroy(b);
}

TEST_CASE("TransformTable try_dense_index rejects an invalid handle") {
  const TransformTable table;
  CHECK_FALSE(table.try_dense_index(EntityId::invalid()).has_value());
}

TEST_CASE("TransformTable swap-and-pop correctness") {
  EntityManager mgr;
  TransformTable table;

  const EntityId e1 = mgr.create();
  const EntityId e2 = mgr.create();
  const EntityId e3 = mgr.create();

  table.insert(e1, 1.f, 2.f, 3.f, 4.f);
  table.insert(e2, 5.f, 6.f, 7.f, 8.f);
  table.insert(e3, 9.f, 10.f, 11.f, 12.f);

  table.remove(e2);

  CHECK(table.has(e1));
  CHECK_FALSE(table.has(e2));
  CHECK(table.has(e3));

  CHECK(table.pos_col(e1) == 1.f);
  CHECK(table.pos_row(e1) == 2.f);
  CHECK(table.pos_col(e3) == 9.f);
  CHECK(table.pos_row(e3) == 10.f);

  table.remove(e1);
  table.remove(e3);
  mgr.destroy(e1);
  mgr.destroy(e2);
  mgr.destroy(e3);
}

TEST_CASE("World spawn and despawn") {
  World w;
  const Position pos{.col = 3.f, .row = 4.f};
  const Velocity vel{.dc = 0.5f, .dr = 0.f};
  const EntityId e = spawn_or_require(w, pos, vel, default_sprite(corundum::sprites::SpriteId{1}));

  CHECK(e.valid());
  CHECK(w.entities.is_live(e));
  CHECK(w.transforms.has(e));
  CHECK(w.sprites.has(e));

  despawn(w, e);
  CHECK_FALSE(w.entities.is_live(e));
  CHECK_FALSE(w.transforms.has(e));
  CHECK_FALSE(w.sprites.has(e));
}

TEST_CASE("World spawn with Animation copies frame counts into the animation table") {
  World w;
  Animation anim{};
  anim.frame_counts[static_cast<std::uint8_t>(corundum::sprites::AnimId::Default)] = 7;

  const Position pos{.col = 1.f, .row = 2.f};
  const Velocity vel{};
  const EntityId e = spawn_or_require(w, pos, vel, default_sprite(corundum::sprites::SpriteId{2}), anim);

  CHECK(w.animations.has(e));
  CHECK(w.animations.frame_count(e, corundum::sprites::AnimId::Default) == 7);
}

TEST_CASE("World mark_for_deletion and flush_deletions") {
  World w;

  const Position pos1{.col = 1.f, .row = 2.f};
  const Position pos2{.col = 3.f, .row = 4.f};
  const Velocity vel{};
  const EntityId e1 = spawn_or_require(w, pos1, vel, default_sprite(corundum::sprites::SpriteId{2}));
  const EntityId e2 = spawn_or_require(w, pos2, vel, default_sprite(corundum::sprites::SpriteId{3}));

  mark_for_deletion(w, e1);
  mark_for_deletion(w, e2);

  CHECK(w.entities.is_live(e1));
  CHECK(w.entities.is_live(e2));

  flush_deletions(w);

  CHECK_FALSE(w.entities.is_live(e1));
  CHECK_FALSE(w.entities.is_live(e2));
  CHECK(w.pending_deletion_count == 0);
}

TEST_CASE("World mark_for_deletion deduplicates double marks") {
  World w;
  const Position pos{.col = 1.f, .row = 2.f};
  const Velocity vel{};
  const EntityId e = spawn_or_require(w, pos, vel, default_sprite(corundum::sprites::SpriteId{2}));
  CHECK(w.entities.is_live(e));

  mark_for_deletion(w, e);
  CHECK(w.pending_deletion_count == 1);
  mark_for_deletion(w, e);
  CHECK(w.pending_deletion_count == 1);

  flush_deletions(w);
  CHECK_FALSE(w.entities.is_live(e));
  CHECK(w.pending_deletion_count == 0);
}

TEST_CASE("World spawn fills the pool to k_max_entities, then reports it full") {
  World w;
  const Sprite sprite = default_sprite(corundum::sprites::SpriteId{1});
  for (std::uint32_t i = 0; i < k_max_entities; ++i)
    REQUIRE(spawn(w, Position{.col = 0.f, .row = 0.f}, Velocity{}, sprite).has_value());

  CHECK(w.entities.full());
  CHECK_FALSE(spawn(w, Position{.col = 0.f, .row = 0.f}, Velocity{}, sprite).has_value());
}

TEST_CASE("World flush_deletions skips an entity despawned directly after it was marked") {
  World w;
  const Position pos{.col = 1.f, .row = 2.f};
  const Velocity vel{};
  const EntityId e = spawn_or_require(w, pos, vel, default_sprite(corundum::sprites::SpriteId{2}));
  mark_for_deletion(w, e);
  despawn(w, e);

  flush_deletions(w);

  CHECK_FALSE(w.entities.is_live(e));
  CHECK(w.pending_deletion_count == 0);
  CHECK(w.entities.alive() == 0u);
}

TEST_CASE("footprint_of — the box is centred on the tile the entity stands on") {
  const GridBox box = footprint_of(8.f, 11.f, 1.f, 0.5f);

  CHECK(box.col == doctest::Approx(8.f));
  CHECK(box.row == doctest::Approx(11.25f));
  CHECK(box.col_span == doctest::Approx(1.f));
  CHECK(box.row_span == doctest::Approx(0.5f));
  // Centred on the sprite's feet anchor — the tile centre — rather than hanging a whole
  // row_span north of it, which is what let a portal one row away fire.
  CHECK(box.col + (box.col_span * 0.5f) == doctest::Approx(8.5f));
  CHECK(box.row + (box.row_span * 0.5f) == doctest::Approx(11.5f));
}

TEST_CASE("position_of — inverts footprint_of") {
  // The pair has to stay in step: the physics solve converts a resolved box back to a
  // position, so a drifted inverse would slide every entity off its own footprint.
  const Position square = position_of(footprint_of(8.f, 11.f, 1.f, 0.5f));
  CHECK(square.col == doctest::Approx(8.f));
  CHECK(square.row == doctest::Approx(11.f));

  const Position narrow = position_of(footprint_of(3.f, 4.f, 0.25f, 0.9f));
  CHECK(narrow.col == doctest::Approx(3.f));
  CHECK(narrow.row == doctest::Approx(4.f));
}
