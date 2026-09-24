// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/schema_version.hpp>
#include <doctest/doctest.h>

#include <expected>
// json_fwd.hpp is include-cleaner's provider for nlohmann::json; json.hpp is still
// required to construct json values (the forward header is incomplete).
#include <nlohmann/json.hpp> // NOLINT(misc-include-cleaner): constructors live in json.hpp
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace core = corundum::core;
using nlohmann::json;

namespace {

  constexpr int k_current = 3;

} // namespace

TEST_CASE("prepare_schema_version: absent field is legacy version 1 and migrates") {
  json root = json::object();
  int called_with = 0;
  const core::SchemaMigration migration = [&called_with](json & /*root*/, int from_version,
                                                         const std::string & /*path*/) {
    called_with = from_version;
    return k_current;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", migration);
  REQUIRE(result.has_value());
  CHECK(called_with == 1);
}

TEST_CASE("prepare_schema_version: current version skips the migration") {
  json root = {{"schema_version", k_current}};
  bool migrated = false;
  const core::SchemaMigration migration = [&migrated](json & /*root*/, int /*from_version*/,
                                                      const std::string & /*path*/) {
    migrated = true;
    return k_current;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", migration);
  REQUIRE(result.has_value());
  CHECK_FALSE(migrated);
}

TEST_CASE("prepare_schema_version: older version migrates from its declared value") {
  json root = {{"schema_version", 2}};
  int called_with = 0;
  const core::SchemaMigration migration = [&called_with](json & /*root*/, int from_version,
                                                         const std::string & /*path*/) {
    called_with = from_version;
    return k_current;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", migration);
  REQUIRE(result.has_value());
  CHECK(called_with == 2);
}

TEST_CASE("prepare_schema_version: a successful migration stamps the current version") {
  json root = json::object();
  const core::SchemaMigration migration = [](json & /*root*/, int /*from_version*/, const std::string & /*path*/) {
    return k_current;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", migration);
  REQUIRE(result.has_value());
  REQUIRE(root.contains("schema_version"));
  CHECK(root["schema_version"].get<int>() == k_current);
}

TEST_CASE("prepare_schema_version: newer version is rejected") {
  json root = {{"schema_version", k_current + 1}};
  const core::SchemaMigration migration = [](json & /*root*/, int /*from_version*/,
                                             const std::string & /*path*/) -> std::expected<int, std::string> {
    FAIL("migration must not run for a newer document");
    return k_current;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", migration);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("newer than this engine supports") != std::string::npos);
}

TEST_CASE("prepare_schema_version: non-positive version is rejected") {
  json root = {{"schema_version", 0}};

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", {});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("invalid schema_version") != std::string::npos);
}

TEST_CASE("prepare_schema_version: non-integer version is rejected") {
  json root = {{"schema_version", "one"}};

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", {});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("must be an integer") != std::string::npos);
}

TEST_CASE("prepare_schema_version: a failing migration propagates its error") {
  json root = {{"schema_version", 1}};
  const core::SchemaMigration failure = [](json & /*root*/, int /*from_version*/,
                                           const std::string & /*path*/) -> std::expected<int, std::string> {
    return std::unexpected(std::string{"migration exploded"});
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", failure);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error() == "migration exploded");
}

TEST_CASE("prepare_schema_version: an older document with no migration hook is rejected") {
  json root = {{"schema_version", 1}};

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", {});
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("no migration") != std::string::npos);
}

TEST_CASE("prepare_schema_version: an absent migration hook accepts a current-version document") {
  // The real loaders pass an empty hook until the first migration exists; a document
  // already at the current version must not require one.
  json root = {{"schema_version", 1}};

  const auto result = core::prepare_schema_version(root, 1, "Thing", "x.json", {});
  CHECK(result.has_value());
}

TEST_CASE("prepare_schema_version: a migration that stops short is rejected") {
  // The failure mode this guards: bumping a format's version constant while leaving the
  // no-op migration stub in place would otherwise let an old document load as current.
  json root = {{"schema_version", 1}};
  const core::SchemaMigration stub = [](json & /*root*/, int from_version, const std::string & /*path*/) {
    return from_version;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", stub);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("stopped at version 1") != std::string::npos);
}

TEST_CASE("prepare_schema_version: a partial migration to an intermediate version is rejected") {
  json root = {{"schema_version", 1}};
  const core::SchemaMigration partial = [](json & /*root*/, int /*from_version*/, const std::string & /*path*/) {
    return 2;
  };

  const auto result = core::prepare_schema_version(root, k_current, "Thing", "x.json", partial);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("stopped at version 2") != std::string::npos);
}
