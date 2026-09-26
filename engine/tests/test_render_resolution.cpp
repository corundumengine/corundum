// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/render_resolution.hpp>

using corundum::core::compute_render_resolution;

TEST_CASE("compute_render_resolution: scale 1 renders at the logical size") {
  const auto resolution = compute_render_resolution(1280, 720, 2560, 1440, 1.f);
  CHECK(resolution.width == 1280);
  CHECK(resolution.height == 720);
}

TEST_CASE("compute_render_resolution: the display content scale renders at native resolution") {
  const auto resolution = compute_render_resolution(2560, 1440, 5120, 2880, 2.f);
  CHECK(resolution.width == 5120);
  CHECK(resolution.height == 2880);
}

TEST_CASE("compute_render_resolution: never exceeds the physical framebuffer") {
  // Non-DPI-scaled display: logical == physical, so asking for 2x still clamps to native.
  const auto resolution = compute_render_resolution(1920, 1080, 1920, 1080, 2.f);
  CHECK(resolution.width == 1920);
  CHECK(resolution.height == 1080);
}

TEST_CASE("compute_render_resolution: sub-1 scales shrink the target") {
  const auto resolution = compute_render_resolution(2560, 1440, 5120, 2880, 0.5f);
  CHECK(resolution.width == 1280);
  CHECK(resolution.height == 720);
}

TEST_CASE("compute_render_resolution: preserves aspect ratio") {
  const auto resolution = compute_render_resolution(2560, 1440, 5120, 2880, 0.75f);
  CHECK(resolution.width == 1920);
  CHECK(resolution.height == 1080);
}

TEST_CASE("compute_render_resolution: degenerate inputs clamp to at least 1x1") {
  const auto zero = compute_render_resolution(0, 0, 0, 0, 1.f);
  CHECK(zero.width == 1);
  CHECK(zero.height == 1);

  const auto tiny = compute_render_resolution(1, 1, 4, 4, 0.25f);
  CHECK(tiny.width == 1);
  CHECK(tiny.height == 1);

  // A zero physical size must not clamp the requested size away.
  const auto no_framebuffer = compute_render_resolution(800, 600, 0, 0, 1.f);
  CHECK(no_framebuffer.width == 800);
  CHECK(no_framebuffer.height == 600);
}
