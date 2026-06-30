#include <corundum/core/json_schema.hpp>

#include <format>
#include <print>
#include <string_view>

using json = nlohmann::json;

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
    "speaker": { "type": "string" },
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

  } // namespace

  // ── SchemaValidator ───────────────────────────────────────────────────────────

  std::expected<SchemaValidator, std::string> SchemaValidator::from_string(std::string_view schema_json) {
    try {
      return SchemaValidator(json::parse(schema_json));
    } catch (const json::parse_error &e) {
      return std::unexpected(std::format("schema parse error: {}", e.what()));
    }
  }

  SchemaValidator::SchemaValidator(json schema_json) {
    try {
      validator_.set_root_schema(schema_json);
    } catch (const std::exception &e) {
      std::println(stderr, "[schema] fatal: {}", e.what());
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

  // ── Pre-built schema accessors (thread-safe after first call) ─────────────────

  const SchemaValidator &dialogue_graph_schema() noexcept {
    static const SchemaValidator s = [] {
      auto r = SchemaValidator::from_string(k_dialogue_graph_schema);
      if (!r) {
        std::println(stderr, "[schema] fatal: {}", r.error());
        std::terminate();
      }
      return std::move(*r);
    }();
    return s;
  }

  const SchemaValidator &quest_schema() noexcept {
    static const SchemaValidator q = [] {
      auto r = SchemaValidator::from_string(k_quest_schema);
      if (!r) {
        std::println(stderr, "[schema] fatal: {}", r.error());
        std::terminate();
      }
      return std::move(*r);
    }();
    return q;
  }

} // namespace corundum::core
