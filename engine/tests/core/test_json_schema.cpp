// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/json_schema.hpp>
#include <nlohmann/json.hpp> // IWYU pragma: keep

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

TEST_CASE("SchemaValidator: from_string rejects JSON that is not a valid schema") {
  const auto validator = corundum::core::SchemaValidator::from_string(R"({"required": 42})");
  REQUIRE_FALSE(validator.has_value());
  CHECK_MESSAGE(validator.error().starts_with("schema compile error: "), validator.error());
}
