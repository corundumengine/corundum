#include <doctest/doctest.h>

#include <corundum/core/json_schema.hpp>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

  CHECK(validator->validate(json{{"id", "ok"}}).has_value());

  auto missing = validator->validate(json{{"name", "no-id"}});
  CHECK_FALSE(missing.has_value());
  CHECK_FALSE(missing.error().empty());

  auto bad_type = validator->validate(json{{"id", 42}});
  CHECK_FALSE(bad_type.has_value());
}

TEST_CASE("SchemaValidator: from_string rejects malformed schema JSON") {
  auto validator = corundum::core::SchemaValidator::from_string("{ not valid json");
  REQUIRE_FALSE(validator.has_value());
  CHECK_FALSE(validator.error().empty());
}

TEST_CASE("SchemaCatalog: schema_catalog() owns pre-built validators for every format") {
  const corundum::core::SchemaCatalog &catalog = corundum::core::schema_catalog();

  auto dialogue_ok = catalog.dialogue_graph_schema().validate(json{{"id", "intro"}, {"nodes", json::array()}});
  CHECK_FALSE(dialogue_ok.has_value()); // nodes must be non-empty per schema

  auto quest_ok = catalog.quest_schema().validate(
      json{{"id", "q1"}, {"name", "Quest"}, {"description", ""}, {"stages", json::array()}});
  CHECK_FALSE(quest_ok.has_value()); // stages must be non-empty per schema

  auto item_ok = catalog.item_schema().validate(json{{"id", "sword"}, {"name", "Sword"}});
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