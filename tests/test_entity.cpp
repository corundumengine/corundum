#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/component/transform_table.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/resources/sprite.hpp>
#include <doctest/doctest.h>

using namespace corundum::gameplay::entity;
using corundum::gameplay::component::Position;
using corundum::gameplay::component::Sprite;
using corundum::gameplay::component::Velocity;

TEST_CASE("EntityId default is invalid") {
  EntityId e{};
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
  const EntityId e = spawn(w, Position{3.f, 4.f}, Velocity{0.5f, 0.f},
                           Sprite{corundum::resources::SpriteId{1}, corundum::resources::AnimId::Default, 0});
  CHECK(e.valid());
  CHECK(w.entities.is_live(e));
  CHECK(w.transforms.has(e));
  CHECK(w.sprites.has(e));

  despawn(w, e);
  CHECK_FALSE(w.entities.is_live(e));
  CHECK_FALSE(w.transforms.has(e));
  CHECK_FALSE(w.sprites.has(e));
}

TEST_CASE("World mark_for_deletion and flush_deletions") {
  World w;

  const EntityId e1 = spawn(w, Position{1.f, 2.f}, Velocity{},
                            Sprite{corundum::resources::SpriteId{2}, corundum::resources::AnimId::Default, 0});
  const EntityId e2 = spawn(w, Position{3.f, 4.f}, Velocity{},
                            Sprite{corundum::resources::SpriteId{3}, corundum::resources::AnimId::Default, 0});

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
  const EntityId e = spawn(w, Position{1.f, 2.f}, Velocity{},
                           Sprite{corundum::resources::SpriteId{2}, corundum::resources::AnimId::Default, 0});
  CHECK(w.entities.is_live(e));

  mark_for_deletion(w, e);
  CHECK(w.pending_deletion_count == 1);
  mark_for_deletion(w, e);
  CHECK(w.pending_deletion_count == 1);

  flush_deletions(w);
  CHECK_FALSE(w.entities.is_live(e));
  CHECK(w.pending_deletion_count == 0);
}

TEST_CASE("EntityManager create pool full on fresh World with k_max_entities spawns") {
  World w;
  std::uint32_t spawned = 0;
  for (; spawned < k_max_entities; ++spawned) {
    const EntityId e = spawn(w, Position{0.f, 0.f}, Velocity{},
                             Sprite{corundum::resources::SpriteId{1}, corundum::resources::AnimId::Default, 0});
    CHECK(e.valid());
  }
  CHECK(w.entities.full());
  CHECK(spawned == k_max_entities);
}
