// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>
#include <nlohmann/json_fwd.hpp>

#include <corundum/core/json_schema.hpp>

#include <string>
#include <string_view>

namespace {

  constexpr std::string_view k_simple_schema = R"({
    "type": "object",
    "required": ["id"],
    "properties": {
      "id": { "type": "string", "minLength": 1 }
    }
  })";

} // namespace

TEST_CASE("SchemaValidator: from_string compiles a schema and validates documents") {
  auto validator = corundum::core::SchemaValidator::from_string(k_simple_schema);
  REQUIRE(validator.has_value());

  CHECK(validator->validate(nlohmann::json{{"id", "ok"}}).has_value());

  auto missing = validator->validate(nlohmann::json{{"name", "no-id"}});
  REQUIRE_FALSE(missing.has_value());
  CHECK_MESSAGE(missing.error().find("required property 'id' not found") != std::string::npos, missing.error());

  auto bad_type = validator->validate(nlohmann::json{{"id", 42}});
  REQUIRE_FALSE(bad_type.has_value());
  CHECK_MESSAGE(bad_type.error().find("At /id ") != std::string::npos, bad_type.error());
}

TEST_CASE("SchemaValidator: from_string rejects malformed schema JSON") {
  auto validator = corundum::core::SchemaValidator::from_string("{ not valid json");
  REQUIRE_FALSE(validator.has_value());
  CHECK_MESSAGE(validator.error().starts_with("schema parse error: "), validator.error());
}

TEST_CASE("SchemaCatalog: schema_catalog() owns pre-built validators for every format") {
  const corundum::core::SchemaCatalog &catalog = corundum::core::schema_catalog();

  auto dialogue_ok =
      catalog.dialogue_graph_schema().validate(nlohmann::json{{"id", "intro"}, {"nodes", nlohmann::json::array()}});
  REQUIRE_FALSE(dialogue_ok.has_value()); // nodes must be non-empty per schema
  CHECK_MESSAGE(dialogue_ok.error().find("At /nodes ") != std::string::npos, dialogue_ok.error());

  auto quest_ok = catalog.quest_schema().validate(
      nlohmann::json{{"id", "q1"}, {"name", "Quest"}, {"description", ""}, {"stages", nlohmann::json::array()}});
  REQUIRE_FALSE(quest_ok.has_value()); // stages must be non-empty per schema
  CHECK_MESSAGE(quest_ok.error().find("At /stages ") != std::string::npos, quest_ok.error());

  const auto item_ok = catalog.item_schema().validate(nlohmann::json{{"id", "sword"}, {"name", "Sword"}});
  CHECK(item_ok.has_value());
}

TEST_CASE("SchemaCatalog: accessors are stable references across calls") {
  const corundum::core::SchemaCatalog &a = corundum::core::schema_catalog();
  const corundum::core::SchemaCatalog &b = corundum::core::schema_catalog();

  CHECK(&a == &b);
  CHECK(&a.dialogue_graph_schema() == &b.dialogue_graph_schema());
  CHECK(&a.quest_schema() == &b.quest_schema());
  CHECK(&a.item_schema() == &b.item_schema());
}