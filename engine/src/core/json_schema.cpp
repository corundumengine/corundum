// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/json_schema.hpp>
#include <corundum/core/verify.hpp>
#include <nlohmann/json-schema.hpp>

#include <exception>
#include <expected>
#include <format>
#include <memory>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <utility>

using nlohmann::json;

namespace corundum::core {

  struct SchemaValidator::Impl {
    nlohmann::json_schema::json_validator validator;
  };

  SchemaValidator::SchemaValidator(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

  SchemaValidator::SchemaValidator(SchemaValidator &&) noexcept = default;
  SchemaValidator &SchemaValidator::operator=(SchemaValidator &&) noexcept = default;
  SchemaValidator::~SchemaValidator() = default;

  std::expected<SchemaValidator, std::string> SchemaValidator::from_string(std::string_view schema_json) {
    json schema;
    try {
      schema = json::parse(schema_json);
    } catch (const json::exception &e) {
      return std::unexpected(std::format("schema parse error: {}", e.what()));
    }
    auto impl = std::make_unique<Impl>();
    try {
      impl->validator.set_root_schema(schema);
    } catch (const std::exception &e) {
      return std::unexpected(std::format("schema compile error: {}", e.what()));
    }
    return SchemaValidator{std::move(impl)};
  }

  SchemaValidator SchemaValidator::from_embedded(std::string_view schema_json) {
    std::expected<SchemaValidator, std::string> compiled = from_string(schema_json);
    core::verify(compiled.has_value(), "SchemaValidator::from_embedded: an embedded schema does not compile");
    return std::move(*compiled);
  }

  std::expected<void, std::string> SchemaValidator::validate(const json &document) const noexcept {
    try {
      impl_->validator.validate(document);
      return {};
    } catch (const std::exception &e) {
      return std::unexpected(std::format("schema violation: {}", e.what()));
    }
  }

} // namespace corundum::core
