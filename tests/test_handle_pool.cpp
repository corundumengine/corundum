#include <corundum/core/containers/handle.hpp>
#include <corundum/core/memory/pool.hpp>
#include <corundum/resources/handle/handle.hpp>
#include <doctest/doctest.h>

using namespace corundum;

namespace {
  struct Widget {
    int value{};
  };
} // namespace

TEST_CASE("Handle<T> default is invalid") {
  core::containers::Handle<Widget> h{};
  CHECK_FALSE(h.valid());
  CHECK(h == core::containers::Handle<Widget>::null());
}

TEST_CASE("Handle<T> equality") {
  core::containers::Handle<Widget> a{1, 2};
  core::containers::Handle<Widget> b{1, 2};
  core::containers::Handle<Widget> c{1, 3};
  CHECK(a == b);
  CHECK(a != c);
}

TEST_CASE("Pool acquire returns valid handle") {
  core::memory::Pool<Widget, 4> pool;
  auto result = pool.acquire(42);
  REQUIRE(result.has_value());
  CHECK(result->valid());
  CHECK(pool.used() == 1);
  CHECK(pool.available() == 3);
}

TEST_CASE("Pool get returns element for live handle") {
  core::memory::Pool<Widget, 4> pool;
  auto h = pool.acquire(99);
  REQUIRE(h.has_value());
  Widget *w = pool.get(*h);
  REQUIRE(w != nullptr);
  CHECK(w->value == 99);
}

TEST_CASE("Pool release invalidates handle") {
  core::memory::Pool<Widget, 4> pool;
  auto h = pool.acquire(7);
  REQUIRE(h.has_value());
  pool.release(*h);
  CHECK(pool.get(*h) == nullptr);
  CHECK(pool.used() == 0);
}

TEST_CASE("Pool stale handle is rejected after slot reuse") {
  core::memory::Pool<Widget, 2> pool;
  auto h1 = pool.acquire(1);
  REQUIRE(h1.has_value());
  pool.release(*h1);
  // Reuse the same slot with a new acquire.
  auto h2 = pool.acquire(2);
  REQUIRE(h2.has_value());
  // h1 now points to a reused slot with a higher generation — must be rejected.
  CHECK(pool.get(*h1) == nullptr);
  Widget *w = pool.get(*h2);
  REQUIRE(w != nullptr);
  CHECK(w->value == 2);
}

TEST_CASE("Pool exhausted returns error") {
  core::memory::Pool<Widget, 2> pool;
  auto h1 = pool.acquire(1);
  auto h2 = pool.acquire(2);
  REQUIRE(h1.has_value());
  REQUIRE(h2.has_value());
  auto h3 = pool.acquire(3);
  CHECK_FALSE(h3.has_value());
}

TEST_CASE("Pool null handle is safe to release and get") {
  core::memory::Pool<Widget, 4> pool;
  core::memory::Pool<Widget, 4>::Handle null_h{};
  CHECK(pool.get(null_h) == nullptr);
  pool.release(null_h); // must not crash
}

TEST_CASE("resources::handle aliases are distinct types") {
  resources::handle::TextureHandle th{1, 1};
  resources::handle::FontHandle fh{1, 1};
  // Static check: these are different types even with identical field values.
  static_assert(!std::is_same_v<decltype(th), decltype(fh)>);
  CHECK(th.valid());
  CHECK(fh.valid());
}
