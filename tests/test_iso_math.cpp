#include <doctest/doctest.h>

#include <corundum/core/math/vec.hpp>
namespace ccm = corundum::core::math;
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

  void write_file(const fs::path &p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << content;
  }

  fs::path temp_dir(std::string_view tag) {
    const auto p = fs::temp_directory_path() / "crpg_test_iso" / tag;
    fs::create_directories(p);
    return p;
  }

  constexpr float k_half_tw = 32.f;
  constexpr float k_half_th = 16.f;
  constexpr float k_x_origin = 0.f;

} // namespace

// ── tile_to_world ─────────────────────────────────────────────────────────────

TEST_CASE("tile_to_world — origin tile (0,0) at x_origin") {
  const auto p = ccm::tile_to_world(0, 0, 0, k_half_tw, k_half_th, 0.f, k_x_origin);
  CHECK(p.x == doctest::Approx(k_x_origin));
  CHECK(p.y == doctest::Approx(0.f));
}

TEST_CASE("tile_to_world — tile (1,0) one step right-down") {
  const auto p = ccm::tile_to_world(1, 0, 0, k_half_tw, k_half_th, 0.f, k_x_origin);
  CHECK(p.x == doctest::Approx(k_half_tw));
  CHECK(p.y == doctest::Approx(k_half_th));
}

TEST_CASE("tile_to_world — tile (0,1) one step left-down") {
  const auto p = ccm::tile_to_world(0, 1, 0, k_half_tw, k_half_th, 0.f, k_x_origin);
  CHECK(p.x == doctest::Approx(-k_half_tw));
  CHECK(p.y == doctest::Approx(k_half_th));
}

TEST_CASE("tile_to_world — tile (n,n) on the vertical axis") {
  constexpr int n = 5;
  const auto p = ccm::tile_to_world(n, n, 0, k_half_tw, k_half_th, 0.f, k_x_origin);
  CHECK(p.x == doctest::Approx(k_x_origin)); // (n-n)*half_tw = 0
  CHECK(p.y == doctest::Approx(2.f * n * k_half_th));
}

TEST_CASE("tile_to_world — x_origin shifts leftmost tile to x=0") {
  // For a map of height H, leftmost tile is (0, H-1). With x_origin = (H-1)*half_tw it lands at x=0.
  constexpr int H = 10;
  const float x_origin = static_cast<float>(H - 1) * k_half_tw;
  const auto p = ccm::tile_to_world(0, H - 1, 0, k_half_tw, k_half_th, 0.f, x_origin);
  CHECK(p.x == doctest::Approx(0.f));
}

TEST_CASE("tile_to_world — elevation lifts tile upward") {
  const auto flat = ccm::tile_to_world(2, 2, 0, k_half_tw, k_half_th, 4.f, k_x_origin);
  const auto raised = ccm::tile_to_world(2, 2, 5, k_half_tw, k_half_th, 4.f, k_x_origin);
  CHECK(raised.x == doctest::Approx(flat.x));
  CHECK(raised.y == doctest::Approx(flat.y - 20.f)); // 5 * 4.f
}

// ── world_to_tile ────────────────────────────────────────────────────────────

TEST_CASE("world_to_tile — round-trips tile_to_world at elevation 0") {
  const auto world = ccm::tile_to_world(3, 4, 0, k_half_tw, k_half_th, 4.f, k_x_origin);
  const auto tile = ccm::world_to_tile(world, 0, k_half_tw, k_half_th, 4.f, k_x_origin);
  CHECK(tile.x == doctest::Approx(3.f));
  CHECK(tile.y == doctest::Approx(4.f));
}

TEST_CASE("world_to_tile — round-trips tile_to_world with elevation, assumed elevation matches") {
  const auto world = ccm::tile_to_world(2, 5, 30, k_half_tw, k_half_th, 4.f, k_x_origin);
  const auto tile = ccm::world_to_tile(world, 30, k_half_tw, k_half_th, 4.f, k_x_origin);
  CHECK(tile.x == doctest::Approx(2.f));
  CHECK(tile.y == doctest::Approx(5.f));
}

TEST_CASE("world_to_tile — round-trips with x_origin shift") {
  constexpr int H = 10;
  const float x_origin = static_cast<float>(H - 1) * k_half_tw;
  const auto world = ccm::tile_to_world(0, H - 1, 0, k_half_tw, k_half_th, 4.f, x_origin);
  const auto tile = ccm::world_to_tile(world, 0, k_half_tw, k_half_th, 4.f, x_origin);
  CHECK(tile.x == doctest::Approx(0.f));
  CHECK(tile.y == doctest::Approx(static_cast<float>(H - 1)));
}

TEST_CASE("world_to_tile — wrong assumed elevation misses the true cell") {
  // A screen point over a raised tile (elevation 30) inverted assuming elevation 0
  // should NOT land back on the same cell — the whole point of testing per-candidate
  // elevation in pick_tile() rather than assuming a single global elevation.
  const auto world = ccm::tile_to_world(2, 5, 30, k_half_tw, k_half_th, 4.f, k_x_origin);
  const auto tile = ccm::world_to_tile(world, 0, k_half_tw, k_half_th, 4.f, k_x_origin);
  CHECK_FALSE((tile.x == doctest::Approx(2.f) && tile.y == doctest::Approx(5.f)));
}

// ── iso_depth_key ────────────────────────────────────────────────────────────

TEST_CASE("iso_depth_key — flat map reproduces plain tx+ty ordering") {
  CHECK(ccm::iso_depth_key(0.f, 0.f, 0.f, k_half_th, 4.f) == doctest::Approx(0.f));
  CHECK(ccm::iso_depth_key(3.f, 4.f, 0.f, k_half_th, 4.f) == doctest::Approx(7.f));
  CHECK(ccm::iso_depth_key(1.f, 0.f, 0.f, k_half_th, 4.f) > ccm::iso_depth_key(0.f, 0.f, 0.f, k_half_th, 4.f));
}

TEST_CASE("iso_depth_key — monotonic in tx+ty at fixed elevation") {
  const float d0 = ccm::iso_depth_key(0.f, 0.f, 3.f, k_half_th, 4.f);
  const float d1 = ccm::iso_depth_key(1.f, 0.f, 3.f, k_half_th, 4.f);
  const float d2 = ccm::iso_depth_key(2.f, 0.f, 3.f, k_half_th, 4.f);
  CHECK(d0 < d1);
  CHECK(d1 < d2);
}

TEST_CASE("iso_depth_key — raised platform occludes lower cliff-bottom neighbor past threshold") {
  // Platform A=(0,0), cliff-bottom neighbor B=(1,0), elevation 0. Flat: A must draw before B (unchanged).
  constexpr float elev_step = 4.f;
  const float depth_b = ccm::iso_depth_key(1.f, 0.f, 0.f, k_half_th, elev_step);

  const float flat_depth_a = ccm::iso_depth_key(0.f, 0.f, 0.f, k_half_th, elev_step);
  CHECK(flat_depth_a < depth_b);

  // Threshold elevation at which A's screen lift equals one grid-step (half_th px): H = half_th / elev_step.
  const float threshold = k_half_th / elev_step;

  const float below_depth_a = ccm::iso_depth_key(0.f, 0.f, threshold * 0.5f, k_half_th, elev_step);
  CHECK(below_depth_a < depth_b); // below threshold: A still draws before B (no occlusion flip yet)

  const float above_depth_a = ccm::iso_depth_key(0.f, 0.f, threshold * 1.5f, k_half_th, elev_step);
  CHECK(above_depth_a > depth_b); // above threshold: A now draws after/on top of B (correct occlusion)
}

TEST_CASE("iso_depth_key — guards against division by zero half_th") {
  CHECK(ccm::iso_depth_key(1.f, 2.f, 5.f, 0.f, 4.f) == doctest::Approx(3.f));
}

// ── chunk_at_iso ─────────────────────────────────────────────────────────────

TEST_CASE("chunk_at_iso — player at world origin maps to chunk (0,0)") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_iso_origin");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  // Isometric position of tile (0,0) = (0, 0) with x_origin = 0
  CHECK(chunk_at_iso(0.f, 0.f, m, k_half_tw, k_half_th) == ChunkCoord{0, 0});
}

TEST_CASE("chunk_at_iso — player at center of world maps to center chunk") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_iso_center");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  const int center_col = 8 * 128;
  const int center_row = 4 * 128;
  const auto iso = ccm::tile_to_world(center_col, center_row, 0, k_half_tw, k_half_th, 0.f, 0.f);
  const ChunkCoord c = chunk_at_iso(iso.x, iso.y, m, k_half_tw, k_half_th);
  CHECK(c.col == 8);
  CHECK(c.row == 4);
}

TEST_CASE("chunk_at_iso — clamped at negative iso position") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_iso_clamp");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;
  // Deeply negative iso position → clamped to (0,0)
  CHECK(chunk_at_iso(-999999.f, -999999.f, m, k_half_tw, k_half_th) == ChunkCoord{0, 0});
}

// ── IsoParams overloads ──────────────────────────────────────────────────────

TEST_CASE("IsoParams tile_to_world agrees with scalar form (zero elevation, zero x_origin)") {
  const ccm::IsoParams iso{k_half_tw, k_half_th, 0.f};
  const auto p = ccm::tile_to_world(3.f, 4.f, 0, iso, 4.f);
  const auto expected = ccm::tile_to_world(3, 4, 0, k_half_tw, k_half_th, 4.f, 0.f);
  CHECK(p.x == doctest::Approx(expected.x));
  CHECK(p.y == doctest::Approx(expected.y));
}

TEST_CASE("IsoParams tile_to_world agrees with scalar form (nonzero elevation, nonzero x_origin)") {
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsoParams iso{k_half_tw, k_half_th, xo};
  const auto p = ccm::tile_to_world(2.f, 5.f, 30, iso, 4.f);
  const auto expected = ccm::tile_to_world(2, 5, 30, k_half_tw, k_half_th, 4.f, xo);
  CHECK(p.x == doctest::Approx(expected.x));
  CHECK(p.y == doctest::Approx(expected.y));
}

TEST_CASE("IsoParams tile_to_world with fractional input") {
  const ccm::IsoParams iso{k_half_tw, k_half_th, 0.f};
  const auto p = ccm::tile_to_world(3.7f, 4.2f, 0, iso, 0.f);
  const float expected_x = (3.7f - 4.2f) * k_half_tw;
  const float expected_y = (3.7f + 4.2f) * k_half_th;
  CHECK(p.x == doctest::Approx(expected_x));
  CHECK(p.y == doctest::Approx(expected_y));
}

TEST_CASE("IsoParams world_to_tile agrees with scalar form") {
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsoParams iso{k_half_tw, k_half_th, xo};
  const ccm::Vec2 world{100.f, 200.f};
  const auto t = ccm::world_to_tile(world, 0, iso, 4.f);
  const auto expected = ccm::world_to_tile(world, 0, k_half_tw, k_half_th, 4.f, xo);
  CHECK(t.x == doctest::Approx(expected.x));
  CHECK(t.y == doctest::Approx(expected.y));
}

TEST_CASE("IsoParams world_to_tile tile_to_world round-trip") {
  const ccm::IsoParams iso{k_half_tw, k_half_th, 0.f};
  const auto world = ccm::tile_to_world(3.f, 4.f, 0, iso, 4.f);
  const auto tile = ccm::world_to_tile(world, 0, iso, 4.f);
  CHECK(tile.x == doctest::Approx(3.f));
  CHECK(tile.y == doctest::Approx(4.f));
}

// ── world_bounds_iso ─────────────────────────────────────────────────────────

TEST_CASE("world_bounds_iso — width and height use their own scale factor") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("iso_bounds_sym");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;
  const auto [w, h] = world_bounds_iso(m, k_half_tw, k_half_th);
  // half_th = half_tw / 2 here (the classic 2:1 diamond ratio), so height should come
  // out to exactly half of width — not equal to it (that was the bug being fixed).
  CHECK(w == doctest::Approx(h * 2.f));
}

TEST_CASE("world_bounds_iso — spawn position within bounds") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("iso_bounds_spawn");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;
  const auto [ww, wh] = world_bounds_iso(m, k_half_tw, k_half_th);

  // Center spawn in isometric world space
  const int center_col = m.chunks_wide * m.chunk_size / 2;
  const int center_row = m.chunks_tall * m.chunk_size / 2;
  const float x_origin = static_cast<float>(m.chunks_tall * m.chunk_size - 1) * k_half_tw;
  const auto spawn = ccm::tile_to_world(center_col, center_row, 0, k_half_tw, k_half_th, 0.f, x_origin);
  CHECK(spawn.x >= 0.f);
  CHECK(spawn.x <= ww);
  CHECK(spawn.y >= 0.f);
  CHECK(spawn.y <= wh);
}
