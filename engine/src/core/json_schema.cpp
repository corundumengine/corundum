// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_schema.hpp>
#include <nlohmann/json_fwd.hpp>

#include "core/warn_log.hpp"

#include <exception>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

using nlohmann::json;

namespace corundum::core {

  // ── Embedded schemas ────────────────────────────────────────────────────────────
  // Keeping them embedded eliminates working-directory and deployment concerns.

  namespace {

    constexpr std::string_view k_dialogue_graph_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/dialogue_graph",
  "type": "object",
  "required": ["id", "nodes"],
  "properties": {
    "id": { "type": "string", "minLength": 1 },
    "type": { "type": "string" },
    "schema_version": { "type": "integer", "minimum": 1 },
    "speaker": { "type": "string" },
    "actor_id": { "type": "string" },
    "variables": { "type": "object" },
    "nodes": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "#/definitions/node" }
    }
  },
  "definitions": {
    "node": {
      "type": "object",
      "required": ["id", "type"],
      "properties": {
        "id": { "type": "string", "minLength": 1 },
        "type": { "type": "string", "enum": ["talk", "choice", "event", "end"] },
        "text": { "type": "string" },
        "next": { "type": "string" },
        "once": { "type": "boolean" },
        "choices": {
          "type": "array",
          "minItems": 1,
          "items": { "$ref": "#/definitions/choice" }
        },
        "actions": {
          "type": "array",
          "items": { "type": "string" }
        },
        "metadata": { "type": "object" }
      },
      "allOf": [
        {
          "if": { "properties": { "type": { "const": "talk" } } },
          "then": { "required": ["text", "next"] }
        },
        {
          "if": { "properties": { "type": { "const": "choice" } } },
          "then": { "required": ["choices"] }
        },
        {
          "if": { "properties": { "type": { "const": "event" } } },
          "then": { "required": ["next", "actions"] }
        }
      ]
    },
    "choice": {
      "type": "object",
      "required": ["label", "target"],
      "properties": {
        "label": { "type": "string", "minLength": 1 },
        "target": { "type": "string", "minLength": 1 },
        "condition": { "type": "string" },
        "sequence": { "type": "string", "enum": ["none", "once", "cycle", "random"] },
        "min_visits": { "type": "integer", "minimum": 1 },
        "actions": {
          "type": "array",
          "items": { "type": "string" }
        }
      }
    }
  }
})";

    constexpr std::string_view k_quest_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/quest",
  "type": "object",
  "required": ["id", "name", "description", "stages"],
  "properties": {
    "id": { "type": "string", "minLength": 1 },
    "type": { "type": "string" },
    "schema_version": { "type": "integer", "minimum": 1 },
    "name": { "type": "string", "minLength": 1 },
    "description": { "type": "string" },
    "stages": {
      "type": "array",
      "minItems": 1,
      "items": { "$ref": "#/definitions/stage" }
    }
  },
  "definitions": {
    "stage": {
      "type": "object",
      "required": ["name", "sequence", "objectives"],
      "properties": {
        "name": { "type": "string", "minLength": 1 },
        "sequence": { "type": "integer", "minimum": 1 },
        "resolved": { "type": "boolean" },
        "failed": { "type": "boolean" },
        "advances_to": {
          "type": "array",
          "items": { "type": "string", "minLength": 1 }
        },
        "auto_advance_to": { "type": "string", "minLength": 1 },
        "objectives": {
          "type": "array",
          "items": { "$ref": "#/definitions/objective" }
        }
      }
    },
    "objective": {
      "type": "object",
      "required": ["text"],
      "properties": {
        "text": { "type": "string" },
        "done_condition": { "type": "string" }
      }
    }
  }
})";

    constexpr std::string_view k_item_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/item",
  "type": "object",
  "required": ["id", "name"],
  "properties": {
    "id":          { "type": "string", "minLength": 1 },
    "name":        { "type": "string", "minLength": 1 },
    "description": { "type": "string" },
    "icon":        { "type": "string" },
    "price":       { "type": "integer", "minimum": 0 },
    "category":    { "type": "string", "enum": ["weapon", "apparel", "potion", "misc"] },
    "weapon":      { "type": "object", "properties": { "damage": { "type": "integer", "minimum": 0 } } },
    "apparel":     { "type": "object", "properties": { "slot": { "type": "string" }, "defense": { "type": "integer", "minimum": 0 } } },
    "potion":      { "type": "object", "properties": { "effect": { "type": "string", "minLength": 1 }, "magnitude": { "type": "integer" } } }
  },
  "allOf": [
    { "if": { "required": ["category"], "properties": { "category": { "const": "weapon"  } } }, "then": { "required": ["weapon"]  } },
    { "if": { "required": ["category"], "properties": { "category": { "const": "apparel" } } }, "then": { "required": ["apparel"] } },
    { "if": { "required": ["category"], "properties": { "category": { "const": "potion"  } } }, "then": { "required": ["potion"], "properties": { "potion": { "required": ["effect"] } } } }
  ]
})";

    constexpr std::string_view k_codex_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/codex",
  "type": "object",
  "required": ["entries"],
  "properties": {
    "schema_version": { "type": "integer", "minimum": 1 },
    "entries": {
      "type": "array",
      "items": { "$ref": "#/definitions/entry" }
    }
  },
  "definitions": {
    "entry": {
      "type": "object",
      "required": ["id", "title"],
      "properties": {
        "id":       { "type": "string", "minLength": 1 },
        "title":    { "type": "string", "minLength": 1 },
        "category": { "type": "string" },
        "body":     { "type": "string" }
      }
    }
  }
})";

    constexpr std::string_view k_location_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/location",
  "type": "object",
  "required": ["locations"],
  "properties": {
    "schema_version": { "type": "integer", "minimum": 1 },
    "locations": {
      "type": "array",
      "items": { "$ref": "#/definitions/location" }
    }
  },
  "definitions": {
    "location": {
      "type": "object",
      "required": ["id", "name"],
      "properties": {
        "id":              { "type": "string", "minLength": 1 },
        "name":            { "type": "string", "minLength": 1 },
        "zone":            { "type": "string" },
        "map":             { "type": "string" },
        "col":             { "type": "number", "minimum": 0 },
        "row":             { "type": "number", "minimum": 0 },
        "return_to_world": { "type": "boolean" }
      }
    }
  }
})";

    constexpr std::string_view k_shop_schema = R"({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "$id": "https://corundum.dev/schemas/shop",
  "type": "object",
  "required": ["shops"],
  "properties": {
    "schema_version": { "type": "integer", "minimum": 1 },
    "shops": {
      "type": "array",
      "items": { "$ref": "#/definitions/shop" }
    }
  },
  "definitions": {
    "shop": {
      "type": "object",
      "required": ["id", "name"],
      "properties": {
        "id":       { "type": "string", "minLength": 1 },
        "name":     { "type": "string", "minLength": 1 },
        "faction":  { "type": "string" },
        "buy_rate": { "type": "number", "minimum": 0, "maximum": 1 },
        "stock": {
          "type": "array",
          "items": { "$ref": "#/definitions/stock" }
        }
      }
    },
    "stock": {
      "type": "object",
      "required": ["item"],
      "properties": {
        "item":  { "type": "string", "minLength": 1 },
        "price": { "type": "integer", "minimum": 0 }
      }
    }
  }
})";

  } // namespace

  // ── SchemaValidator ───────────────────────────────────────────────────────────

  std::expected<SchemaValidator, std::string> SchemaValidator::from_string(std::string_view schema_json) {
    try {
      return SchemaValidator(json::parse(schema_json));
    } catch (const std::exception &e) {
      return std::unexpected(std::string("schema parse error: ") + e.what());
    }
  }

  SchemaValidator::SchemaValidator(const json &schema_json) {
    try {
      validator_.set_root_schema(schema_json);
    } catch (const std::exception &e) {
      corundum::detail::warn_log("[schema] fatal: {}", e.what());
      std::terminate();
    }
  }

  std::expected<void, std::string> SchemaValidator::validate(const json &document) const noexcept {
    try {
      validator_.validate(document);
      return {};
    } catch (const std::exception &e) {
      return std::unexpected(std::string(e.what()));
    }
  }

  // ── SchemaCatalog ─────────────────────────────────────────────────────────────

  SchemaCatalog SchemaCatalog::create() {
    SchemaCatalog c;
    c.dialogue_ = SchemaCatalog::compile_or_terminate(k_dialogue_graph_schema);
    c.quest_ = SchemaCatalog::compile_or_terminate(k_quest_schema);
    c.item_ = SchemaCatalog::compile_or_terminate(k_item_schema);
    c.codex_ = SchemaCatalog::compile_or_terminate(k_codex_schema);
    c.location_ = SchemaCatalog::compile_or_terminate(k_location_schema);
    c.shop_ = SchemaCatalog::compile_or_terminate(k_shop_schema);
    return c;
  }

  SchemaValidator SchemaCatalog::compile_or_terminate(std::string_view schema_json) {
    auto result = SchemaValidator::from_string(schema_json);
    if (!result) {
      corundum::detail::warn_log("[schema] fatal: {}", result.error());
      std::terminate();
    }
    return std::move(*result);
  }

  const SchemaValidator &SchemaCatalog::dialogue_graph_schema() const noexcept {
    return dialogue_;
  }

  const SchemaValidator &SchemaCatalog::quest_schema() const noexcept {
    return quest_;
  }

  const SchemaValidator &SchemaCatalog::item_schema() const noexcept {
    return item_;
  }

  const SchemaValidator &SchemaCatalog::codex_schema() const noexcept {
    return codex_;
  }

  const SchemaValidator &SchemaCatalog::location_schema() const noexcept {
    return location_;
  }

  const SchemaValidator &SchemaCatalog::shop_schema() const noexcept {
    return shop_;
  }

  const SchemaCatalog &schema_catalog() noexcept {
    static const SchemaCatalog catalog = [] noexcept -> SchemaCatalog {
      try {
        return SchemaCatalog::create();
      } catch (...) {
        std::terminate();
      }
    }();
    return catalog;
  }

} // namespace corundum::core
