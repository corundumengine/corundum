// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/actor_id_table.hpp>
#include <corundum/entities/tables/table_concepts.hpp>

#include <string>

using corundum::entities::ActorIdTable;
using corundum::entities::EntityId;
using corundum::entities::EntityManager;

static_assert(corundum::entities::GameTable<ActorIdTable>);

TEST_CASE("ActorIdTable insert/has/get") {
  EntityManager mgr;
  ActorIdTable table;

  const EntityId e = mgr.create();
  CHECK_FALSE(table.has(e));

  table.insert(e, "brann");
  CHECK(table.has(e));
  CHECK(table.get_actor_id(e) == "brann");
  CHECK(table.count == 1);

  table.remove(e);
  CHECK_FALSE(table.has(e));
  CHECK(table.count == 0);

  mgr.destroy(e);
}

TEST_CASE("ActorIdTable get returns a non-owning view") {
  EntityManager mgr;
  ActorIdTable table;

  const EntityId e = mgr.create();
  table.insert(e, "brann");
  const std::string_view id = table.get_actor_id(e);
  CHECK(id == "brann");
  table.remove(e);

  mgr.destroy(e);
}

TEST_CASE("ActorIdTable swap-and-pop preserves the surviving row") {
  EntityManager mgr;
  ActorIdTable table;

  const EntityId e1 = mgr.create();
  const EntityId e2 = mgr.create();
  const EntityId e3 = mgr.create();

  table.insert(e1, "first");
  table.insert(e2, "middle");
  table.insert(e3, "last");

  table.remove(e2);

  CHECK_FALSE(table.has(e2));
  CHECK(table.has(e1));
  CHECK(table.has(e3));
  CHECK(table.get_actor_id(e1) == "first");
  CHECK(table.get_actor_id(e3) == "last");
  CHECK(table.count == 2);

  table.remove(e1);
  table.remove(e3);
  CHECK(table.count == 0);

  mgr.destroy(e1);
  mgr.destroy(e2);
  mgr.destroy(e3);
}

TEST_CASE("ActorIdTable truncates over-long ids") {
  EntityManager mgr;
  ActorIdTable table;

  const EntityId e = mgr.create();
  const std::string long_id(ActorIdTable::k_max_id_len * 2, 'x');
  table.insert(e, long_id);
  CHECK(table.get_actor_id(e).size() == ActorIdTable::k_max_id_len - 1);
  table.remove(e);

  mgr.destroy(e);
}