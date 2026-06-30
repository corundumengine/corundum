#pragma once
#include <expected>
#include <nlohmann/json-schema.hpp>
#include <string>
#include <string_view>

namespace corundum::core {

  /// Load-once, validate-many JSON Schema validator.
  ///
  /// Parses a JSON Schema document and caches the compiled validator. Call
  /// validate() on each document; errors include the offending field path
  /// when the schema library provides it.
  ///
  /// @note Not safe for concurrent use (nlohmann::json_schema::json_validator is not thread-safe).
  class SchemaValidator {
  public:
    SchemaValidator() = default;
    SchemaValidator(SchemaValidator &&) = default;
    SchemaValidator &operator=(SchemaValidator &&) = default;

    SchemaValidator(const SchemaValidator &) = delete;
    SchemaValidator &operator=(const SchemaValidator &) = delete;

    /// Construct from a JSON Schema string (e.g. an embedded string literal).
    /// @param schema_json A valid JSON Schema document as a string.
    /// @return The validator on success, or an error message on failure.
    [[nodiscard]] static std::expected<SchemaValidator, std::string> from_string(std::string_view schema_json);

    /// Validate a JSON document against the loaded schema.
    /// @param document The JSON document to validate.
    /// @return ok on success, or an error description on failure.
    [[nodiscard]] std::expected<void, std::string> validate(const nlohmann::json &document) const noexcept;

  private:
    explicit SchemaValidator(nlohmann::json schema_json);

    nlohmann::json_schema::json_validator validator_;
  };

  /// Returns a pre-built SchemaValidator for dialogue graph JSON files.
  /// The validator is constructed once on first call (thread-safe since C++11).
  /// @return Reference to a static SchemaValidator initialised from the embedded dialogue schema.
  /// @pre The embedded schema string must be valid JSON Schema (enforced via std::terminate on failure).
  [[nodiscard]] const SchemaValidator &dialogue_graph_schema() noexcept;

  /// Returns a pre-built SchemaValidator for quest JSON files.
  /// The validator is constructed once on first call (thread-safe since C++11).
  /// @return Reference to a static SchemaValidator initialised from the embedded quest schema.
  [[nodiscard]] const SchemaValidator &quest_schema() noexcept;

} // namespace corundum::core
