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
    SchemaValidator(SchemaValidator &&) noexcept = default;
    SchemaValidator &operator=(SchemaValidator &&) noexcept = default;

    SchemaValidator(const SchemaValidator &) = delete;
    SchemaValidator &operator=(const SchemaValidator &) = delete;
    ~SchemaValidator() = default;

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

  /// Owns the pre-built SchemaValidators for every asset format the engine
  /// validates (dialogue graphs, quests, items). Each validator is compiled once
  /// from its embedded schema at construction and reused for every document.
  ///
  /// @note Not safe for concurrent use (see SchemaValidator). Construct via
  ///       schema_catalog() once; do not create multiple catalogs.
  class SchemaCatalog {
  public:
    SchemaCatalog() = default;
    SchemaCatalog(SchemaCatalog &&) noexcept = default;
    SchemaCatalog &operator=(SchemaCatalog &&) noexcept = default;

    SchemaCatalog(const SchemaCatalog &) = delete;
    SchemaCatalog &operator=(const SchemaCatalog &) = delete;
    ~SchemaCatalog() = default;

    /// Returns the pre-built validator for dialogue graph JSON files.
    /// @return Reference to the validator compiled from the embedded dialogue schema.
    [[nodiscard]] const SchemaValidator &dialogue_graph_schema() const noexcept;

    /// Returns the pre-built validator for quest JSON files.
    /// @return Reference to the validator compiled from the embedded quest schema.
    [[nodiscard]] const SchemaValidator &quest_schema() const noexcept;

    /// Returns the pre-built validator for item JSON files.
    /// @return Reference to the validator compiled from the embedded item schema.
    [[nodiscard]] const SchemaValidator &item_schema() const noexcept;

  private:
    friend const SchemaCatalog &schema_catalog() noexcept;

    /// Builds a fully-populated catalog, compiling every embedded schema.
    /// @return A SchemaCatalog with all validators compiled.
    /// @pre All embedded schema strings must be valid JSON Schema; enforced via std::terminate on failure.
    [[nodiscard]] static SchemaCatalog create();

    /// Compiles a validator from an embedded schema string, terminating on failure.
    /// @param schema_json A valid JSON Schema document as a string.
    /// @return The compiled validator.
    /// @pre schema_json must be valid JSON Schema; enforced via std::terminate on failure.
    [[nodiscard]] static SchemaValidator compile_or_terminate(std::string_view schema_json);

    SchemaValidator dialogue_;
    SchemaValidator quest_;
    SchemaValidator item_;
  };

  /// Returns the engine-wide schema catalog, constructed once on first call
  /// (thread-safe since C++11).
  /// @return Reference to the static SchemaCatalog with all embedded validators.
  /// @pre All embedded schema strings must be valid JSON Schema (enforced via std::terminate on failure).
  [[nodiscard]] const SchemaCatalog &schema_catalog() noexcept;

} // namespace corundum::core
