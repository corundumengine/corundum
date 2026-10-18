// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include "memory_content_source.hpp"
#include "null_platform.hpp"
#include <corundum/engine.hpp>

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

  using corundum::Engine;

  constexpr std::string_view k_marker_path{"corundum_content_source_test_marker.txt"};

} // namespace

TEST_CASE("Engine: set_content_source replaces the source before a platform is adopted") {
  auto engine = std::make_unique<Engine>();
  auto source = std::make_unique<corundum::test::MemoryContentSource>();
  source->add(k_marker_path, "marker");

  CHECK_FALSE(engine->content_source().contains(k_marker_path));
  REQUIRE(engine->set_content_source(std::move(source)).has_value());
  CHECK(engine->content_source().contains(k_marker_path));
}

TEST_CASE("Engine: set_content_source refuses a source once a platform is adopted") {
  auto engine = std::make_unique<Engine>();
  corundum::platform::null::NullPlatform platform = corundum::platform::null::make_null_platform(320, 240);
  corundum::platform::null::adopt_null_platform(*engine, platform);

  const std::expected<void, std::string> result =
      engine->set_content_source(std::make_unique<corundum::test::MemoryContentSource>());
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("platform") != std::string::npos);
}

TEST_CASE("Engine: set_content_source refuses a null source") {
  auto engine = std::make_unique<Engine>();

  const std::expected<void, std::string> result = engine->set_content_source(nullptr);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().find("non-null") != std::string::npos);
}
