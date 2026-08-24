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

  const ccm::IsometricParams iso{k_half_tw, k_half_th, 0.f, 4.f};
  // Isometric position of tile (0,0) = (0, 0) with x_origin = 0
  CHECK(chunk_at_iso(0.f, 0.f, m, iso) == ChunkCoord{0, 0});
}

TEST_CASE("chunk_at_iso — player at center of world maps to center chunk") {
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_iso_center");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  const ccm::IsometricParams iso{k_half_tw, k_half_th, 0.f, 4.f};
  const int center_col = 8 * 128;
  const int center_row = 4 * 128;
  const auto world = ccm::tile_to_world(center_col, center_row, 0, iso);
  const ChunkCoord c = chunk_at_iso(world.x, world.y, m, iso);
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
  const ccm::IsometricParams iso{k_half_tw, k_half_th, 0.f, 4.f};
  // Deeply negative iso position → clamped to (0,0)
  CHECK(chunk_at_iso(-999999.f, -999999.f, m, iso) == ChunkCoord{0, 0});
}

TEST_CASE("chunk_at_iso — center tile with non-zero x_origin selects the center chunk") {
  // Bug: chunk_at_iso's inverse ignored x_origin. iso coords from tile_to_world()
  // include x_origin, so the recovered col_f was pc + (height-1)/2 — diagonal
  // shift away from the true chunk. Reproduces the engine caller's setup
  // (sync_active_chunks in render_sys.cpp) and asserts the correct center chunk
  // is selected.
  using namespace corundum::gameplay::world::tilemap;
  const auto dir = temp_dir("chunk_iso_x_origin");
  write_file(dir / "manifest.json", R"({"chunk_size":128,"chunks_wide":16,"chunks_tall":8})");
  auto manifest_result = load_world_manifest(dir / "manifest.json");
  REQUIRE(manifest_result.has_value());
  const auto &m = *manifest_result;

  const int total_h = m.chunks_tall * m.chunk_size;
  const auto iso = ccm::compute_isometric_params(/*diamond_w=*/64, /*diamond_h=*/32, total_h, /*tile_scale=*/1.f,
                                                 /*elev_step=*/4.f);
  const int center_col = m.chunks_wide * m.chunk_size / 2;
  const int center_row = m.chunks_tall * m.chunk_size / 2;
  const auto world = ccm::tile_to_world(center_col, center_row, 0, iso);

  const ChunkCoord c = chunk_at_iso(world.x, world.y, m, iso);
  CHECK(c.col == m.chunks_wide / 2);
  CHECK(c.row == m.chunks_tall / 2);
}

// ── IsometricParams overloads ──────────────────────────────────────────────────────

TEST_CASE("IsometricParams tile_to_world agrees with scalar form (zero elevation, zero x_origin)") {
  const ccm::IsometricParams iso{k_half_tw, k_half_th, 0.f, 4.f};
  const auto p = ccm::tile_to_world(3.f, 4.f, 0, iso);
  const auto expected = ccm::tile_to_world(3, 4, 0, k_half_tw, k_half_th, 4.f, 0.f);
  CHECK(p.x == doctest::Approx(expected.x));
  CHECK(p.y == doctest::Approx(expected.y));
}

TEST_CASE("IsometricParams tile_to_world agrees with scalar form (nonzero elevation, nonzero x_origin)") {
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsometricParams iso{k_half_tw, k_half_th, xo, 4.f};
  const auto p = ccm::tile_to_world(2.f, 5.f, 30, iso);
  const auto expected = ccm::tile_to_world(2, 5, 30, k_half_tw, k_half_th, 4.f, xo);
  CHECK(p.x == doctest::Approx(expected.x));
  CHECK(p.y == doctest::Approx(expected.y));
}

TEST_CASE("IsometricParams tile_to_world with fractional input") {
  const ccm::IsometricParams iso{k_half_tw, k_half_th, 0.f, 4.f};
  const auto p = ccm::tile_to_world(3.7f, 4.2f, 0, iso);
  const float expected_x = (3.7f - 4.2f) * k_half_tw;
  const float expected_y = (3.7f + 4.2f) * k_half_th;
  CHECK(p.x == doctest::Approx(expected_x));
  CHECK(p.y == doctest::Approx(expected_y));
}

// ── compute_isometric_params — elev_step scales with tile_scale ───────────────

TEST_CASE("compute_isometric_params — elev_step scales linearly with tile_scale") {
  // Default tile_scale=1.f is the no-op baseline (matches every existing test that
  // hand-builds an IsometricParams and asserts raw elev_step).
  const auto iso1 = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/1.f, /*elev_step=*/4.f);
  CHECK(iso1.elev_step == doctest::Approx(4.f));

  // tile_scale=2.f → elev_step doubles, matching the default GameConfig::tile_scale.
  const auto iso2 = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/2.f, /*elev_step=*/4.f);
  CHECK(iso2.elev_step == doctest::Approx(8.f));

  // tile_scale=0.5f halves it.
  const auto iso_half = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/0.5f, /*elev_step=*/4.f);
  CHECK(iso_half.elev_step == doctest::Approx(2.f));

  // half_tw / half_th are scaled the same way — sanity check the field convention.
  CHECK(iso2.half_tw == doctest::Approx(iso1.half_tw * 2.f));
  CHECK(iso2.half_th == doctest::Approx(iso1.half_th * 2.f));
}

TEST_CASE("compute_isometric_params — elev_step/half_th is scale-invariant (no zoom drift)") {
  // The "no relative drift when zooming" property: the elevation ratio
  // elev_step / half_th must be constant across tile_scale values, since
  // elevation lift should scale together with the rest of the diamond geometry.
  // iso_depth_key uses this ratio as its elevation term, so scale-invariance
  // here is what keeps depth ordering stable across zoom levels.
  constexpr float elev_step = 4.f;
  const auto iso_1x = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/1.f, elev_step);
  const auto iso_2x = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/2.f, elev_step);
  const auto iso_half = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/0.5f, elev_step);

  const float r1 = iso_1x.elev_step / iso_1x.half_th;
  const float r2 = iso_2x.elev_step / iso_2x.half_th;
  const float rh = iso_half.elev_step / iso_half.half_th;

  CHECK(r1 == doctest::Approx(r2));
  CHECK(r1 == doctest::Approx(rh));
}

TEST_CASE("compute_isometric_params — tile_to_world elevation lift scales with tile_scale") {
  // End-to-end check: an elevated tile at (2,2) lifts by exactly elev * iso.elev_step
  // in scaled coordinates, and iso.elev_step scales linearly with tile_scale — so
  // elevation lift scales with the rest of the diamond geometry instead of drifting
  // relative to it (which is the user-visible "stays locked to its ground cell while
  // zooming" guarantee from the renderer side).
  constexpr float elev_step = 4.f;
  constexpr int elev = 5;
  const auto iso_1x = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/1.f, elev_step);
  const auto iso_2x = ccm::compute_isometric_params(64, 32, 10, /*tile_scale=*/2.f, elev_step);

  const auto flat_1x = ccm::tile_to_world(2, 2, 0, iso_1x);
  const auto raised_1x = ccm::tile_to_world(2, 2, elev, iso_1x);
  const auto flat_2x = ccm::tile_to_world(2, 2, 0, iso_2x);
  const auto raised_2x = ccm::tile_to_world(2, 2, elev, iso_2x);

  const float lift_1x = flat_1x.y - raised_1x.y;
  const float lift_2x = flat_2x.y - raised_2x.y;
  CHECK(lift_1x == doctest::Approx(static_cast<float>(elev) * iso_1x.elev_step));
  CHECK(lift_2x == doctest::Approx(static_cast<float>(elev) * iso_2x.elev_step));
  // Lift grows in proportion to tile_scale, matching the rest of the diamond geometry.
  CHECK(lift_2x == doctest::Approx(lift_1x * 2.f));
}

// ── tile_to_world_center (anchor unification) ───────────────────────────────

TEST_CASE("tile_to_world_center — cell center is top vertex plus (0, half_th)") {
  // Pins down the unified cell-center anchor convention that 243764a introduced
  // for tile sprites but never tested. Without this, entity/camera/debug-overlay
  // sites drift back to top-vertex (or worse, top-vertex-minus-half_th) anchoring
  // and actors float half_th above the ground or appear sunk below it.
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsometricParams iso{k_half_tw, k_half_th, xo, 4.f};

  const auto top = ccm::tile_to_world(5, 5, 0, iso);
  const auto center = ccm::tile_to_world_center(5.f, 5.f, 0.f, iso);

  CHECK(center.x == doctest::Approx(top.x));
  CHECK(center.y == doctest::Approx(top.y + iso.half_th));
}

TEST_CASE("tile_to_world_center — entity anchor equals tile sprite cell-center anchor") {
  // The actual regression: an entity standing at (col, row) must produce the same
  // screen anchor as a tile sprite rendered at (col, row) — both at cell center.
  // Tile sprites use tile_to_world + (0, half_th); entities use the same point.
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsometricParams iso{k_half_tw, k_half_th, xo, 4.f};

  const auto entity_anchor = ccm::tile_to_world_center(5.f, 5.f, 0.f, iso);
  const auto tile_top = ccm::tile_to_world(5, 5, 0, iso);
  const auto tile_center = ccm::Vec2{tile_top.x, tile_top.y + iso.half_th};

  CHECK(entity_anchor.x == doctest::Approx(tile_center.x));
  CHECK(entity_anchor.y == doctest::Approx(tile_center.y));
}

TEST_CASE("tile_to_world_center — elevation lifts the cell center, not just the top vertex") {
  // The cell center must track the top vertex exactly half_th below it even
  // when elevation is non-zero — otherwise raised platforms mis-anchor entities
  // by half_th at every elevation level.
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsometricParams iso{k_half_tw, k_half_th, xo, 4.f};
  constexpr int elev = 30;

  const auto center_e0 = ccm::tile_to_world_center(5.f, 5.f, 0.f, iso);
  const auto center_e = ccm::tile_to_world_center(5.f, 5.f, static_cast<float>(elev), iso);
  CHECK(center_e.y == doctest::Approx(center_e0.y - static_cast<float>(elev) * iso.elev_step));
}

TEST_CASE("IsometricParams world_to_tile agrees with scalar form") {
  constexpr float xo = 7.f * k_half_tw;
  const ccm::IsometricParams iso{k_half_tw, k_half_th, xo, 4.f};
  const ccm::Vec2 world{100.f, 200.f};
  const auto t = ccm::world_to_tile(world, 0, iso);
  const auto expected = ccm::world_to_tile(world, 0, k_half_tw, k_half_th, 4.f, xo);
  CHECK(t.x == doctest::Approx(expected.x));
  CHECK(t.y == doctest::Approx(expected.y));
}

TEST_CASE("IsometricParams world_to_tile tile_to_world round-trip") {
  const ccm::IsometricParams iso{k_half_tw, k_half_th, 0.f, 4.f};
  const auto world = ccm::tile_to_world(3.f, 4.f, 0, iso);
  const auto tile = ccm::world_to_tile(world, 0, iso);
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

// ── IsometricCullBounds ───────────────────────────────────────────────────

static constexpr float k_test_half_tw = 32.f;
static constexpr float k_test_half_th = 16.f;
static constexpr float k_test_x_origin = 128.f;
static constexpr float k_test_elev_step = 4.f;

TEST_CASE("IsometricCullBounds — round-trip: rect containing a tile encloses its depth and column") {
  const ccm::IsometricParams iso{k_test_half_tw, k_test_half_th, k_test_x_origin, k_test_elev_step};

  for (int col = 0; col < 10; ++col) {
    for (int row = 0; row < 10; ++row) {
      const ccm::Vec2 world = ccm::tile_to_world(col, row, 0, iso.half_tw, iso.half_th, 0.f, iso.x_origin);
      const float margin = 1.f;
      const ccm::IsometricCullBounds cull = ccm::compute_isometric_cull_bounds(world.x - margin, world.y - margin,
                                                                               world.x + margin, world.y + margin, iso);

      INFO("col=", col, " row=", row);
      CHECK(cull.depth_min <= col + row);
      CHECK(cull.depth_max >= col + row);
      const int first_col = ccm::isometric_cull_first_column(cull, col + row);
      const int last_col = ccm::isometric_cull_last_column(cull, col + row);
      CHECK(first_col <= col);
      CHECK(last_col >= col);
    }
  }
}

TEST_CASE("IsometricCullBounds — brute-force conservativeness over 64×64") {
  const ccm::IsometricParams iso{k_test_half_tw, k_test_half_th, k_test_x_origin, k_test_elev_step};
  const float half_w = static_cast<float>(k_test_half_tw);
  const float half_h = static_cast<float>(k_test_half_th);

  // Sweep a small viewport across the tile grid to verify every tile anchor
  // inside the rect is also inside the cull bounds.
  for (int offset_x = 0; offset_x < 10; ++offset_x) {
    for (int offset_y = 0; offset_y < 10; ++offset_y) {
      const float cx = k_test_x_origin + static_cast<float>(offset_x) * half_w * 2.f;
      const float cy = static_cast<float>(offset_y) * half_h * 2.f;
      const float vpw = half_w * 20.f;
      const float vph = half_h * 20.f;

      const ccm::IsometricCullBounds cull = ccm::compute_isometric_cull_bounds(cx, cy, cx + vpw, cy + vph, iso);

      int enclosed = 0;
      for (int col = 0; col < 64; ++col) {
        for (int row = 0; row < 64; ++row) {
          const ccm::Vec2 world = ccm::tile_to_world(col, row, 0, iso.half_tw, iso.half_th, 0.f, iso.x_origin);
          const bool in_rect = world.x >= cx && world.x <= cx + vpw && world.y >= cy && world.y <= cy + vph;
          const int depth = col + row;
          bool in_cull = depth >= cull.depth_min && depth <= cull.depth_max;
          if (in_cull) {
            const int first_col = ccm::isometric_cull_first_column(cull, depth);
            const int last_col = ccm::isometric_cull_last_column(cull, depth);
            in_cull = col >= first_col && col <= last_col;
          }
          if (in_rect) {
            REQUIRE_MESSAGE(in_cull,
                            "offset_x=" << offset_x << " offset_y=" << offset_y << " col=" << col << " row=" << row);
            ++enclosed;
          }
        }
      }
      CHECK(enclosed > 0);

      // Bounds should add at most ~2 spare bands per edge
      const int max_bands = 64 + 64 - 1;
      const int visible_bands = cull.depth_max - cull.depth_min + 1;
      CHECK(visible_bands <= max_bands);
      CHECK(visible_bands > 0);
    }
  }
}

TEST_CASE("IsometricCullBounds — degenerate params return culls-nothing sentinel") {
  const ccm::IsometricParams iso_zero{0.f, 0.f, 0.f, 0.f};
  const ccm::IsometricCullBounds cull = ccm::compute_isometric_cull_bounds(0.f, 0.f, 100.f, 100.f, iso_zero);
  CHECK(cull.depth_min <= -1000000000);
  CHECK(cull.depth_max >= 1000000000);

  const ccm::IsometricParams iso_neg{-1.f, -1.f, 0.f, 0.f};
  const ccm::IsometricCullBounds cull_neg = ccm::compute_isometric_cull_bounds(0.f, 0.f, 100.f, 100.f, iso_neg);
  CHECK(cull_neg.depth_min <= -1000000000);
  CHECK(cull_neg.depth_max >= 1000000000);
}
