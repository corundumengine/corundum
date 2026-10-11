// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/texture_ref_counts.hpp>

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using corundum::platform::TextureRefCounts;

using Ids = std::vector<std::uint32_t>;

TEST_CASE("TextureRefCounts: a released id is unreferenced but not taken until take_unreferenced") {
  TextureRefCounts refs;
  refs.acquire(3);
  refs.release(3);

  CHECK_EQ(refs.count(3), 0u);
  CHECK_EQ(refs.take_unreferenced(), Ids{3});
}

TEST_CASE("TextureRefCounts: take_unreferenced returns each id once") {
  TextureRefCounts refs;
  refs.acquire(3);
  refs.release(3);

  CHECK_EQ(refs.take_unreferenced(), Ids{3});
  CHECK_EQ(refs.take_unreferenced(), Ids{});
}

TEST_CASE("TextureRefCounts: re-acquiring before take keeps the id alive") {
  TextureRefCounts refs;
  refs.acquire(5);
  refs.release(5);
  refs.acquire(5);

  CHECK_EQ(refs.take_unreferenced(), Ids{});
  CHECK_EQ(refs.count(5), 1u);
}

TEST_CASE("TextureRefCounts: id 0 and untracked ids are ignored") {
  TextureRefCounts refs;
  refs.acquire(0);
  refs.release(0);
  refs.release(9);

  CHECK_EQ(refs.count(0), 0u);
  CHECK_EQ(refs.count(9), 0u);
  CHECK_EQ(refs.take_unreferenced(), Ids{});
}

TEST_CASE("TextureRefCounts: release never drops below zero") {
  TextureRefCounts refs;
  refs.acquire(2);
  refs.release(2);
  refs.release(2);
  refs.acquire(2);

  CHECK_EQ(refs.count(2), 1u);
}

TEST_CASE("TextureRefCounts: a purged id cannot be acquired again") {
  TextureRefCounts refs;
  refs.acquire(4);
  refs.release(4);
  CHECK_EQ(refs.take_unreferenced(), Ids{4});
  refs.acquire(4);

  CHECK_EQ(refs.count(4), 0u);
  CHECK_EQ(refs.take_unreferenced(), Ids{});
}
