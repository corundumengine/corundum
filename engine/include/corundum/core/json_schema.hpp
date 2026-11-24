// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <nlohmann/json_fwd.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace corundum::core {

/** A compiled JSON Schema: compile once, validate many documents.
 *  @note Not safe for concurrent use; give each thread its own validator.
 */
class SchemaValidator {
public:
  SchemaValidator(SchemaValidator &&) noexcept;
  SchemaValidator &operator=(SchemaValidator &&) noexcept;
  SchemaValidator(const SchemaValidator &) = delete;
  SchemaValidator &operator=(const SchemaValidator &) = delete;
  ~SchemaValidator();

  /** @return The compiled validator, or an error when `schema_json` is not JSON
   *          ("schema parse error: …") or not a valid schema ("schema compile error: …").
   */
  [[nodiscard]] static std::expected<SchemaValidator, std::string> from_string(std::string_view schema_json);

  /** Compile a schema embedded in the engine binary.
   *  @pre `schema_json` is a valid schema. A failure is a build defect, not a content error,
   *       so it aborts through core::verify in every build type.
   */
  [[nodiscard]] static SchemaValidator from_embedded(std::string_view schema_json);

  /** @return ok, or the validator's description of the first violation, with the field path
   *          when the schema library reports one.
   */
  [[nodiscard]] std::expected<void, std::string> validate(const nlohmann::json &document) const noexcept;

private:
  struct Impl;

  explicit SchemaValidator(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

} // namespace corundum::core
