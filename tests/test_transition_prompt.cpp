#include <doctest/doctest.h>

#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/portals/transition_prompt.hpp>
#include <corundum/input/actions.hpp>

namespace {

  corundum::gameplay::world::Portal make_portal() {
    corundum::gameplay::world::Portal p{};
    p.col = 13.f;
    p.row = 13.f;
    p.w = 1.f;
    p.h = 1.f;
    p.target_map = "tests/fixtures/tilemaps/interior.json";
    p.spawn_col = 1;
    p.spawn_row = 2;
    p.return_to_world = false;
    return p;
  }

  corundum::input::InputState with_only(corundum::input::Action action) {
    corundum::input::InputState input{};
    input.pressed.set(static_cast<std::size_t>(action));
    return input;
  }

} // namespace

using corundum::gameplay::world::Portal;
using corundum::gameplay::world::TransitionPrompt;
using Step = TransitionPrompt::Step;

TEST_CASE("TransitionPrompt — fresh arm highlights Yes and is not declined") {
  TransitionPrompt prompt{make_portal()};
  CHECK(prompt.confirm_selected());
  CHECK_FALSE(prompt.declined());
}

TEST_CASE("TransitionPrompt — MoveRight moves highlight to No and stays pending") {
  TransitionPrompt prompt{make_portal()};
  const Step result = prompt.step(with_only(corundum::input::Action::MoveRight));
  CHECK(result == Step::Pending);
  CHECK_FALSE(prompt.confirm_selected());

  prompt.step(with_only(corundum::input::Action::MoveLeft));
  CHECK(prompt.confirm_selected());
}

TEST_CASE("TransitionPrompt — MoveUp/Down alias MoveLeft/Right") {
  TransitionPrompt prompt{make_portal()};
  prompt.step(with_only(corundum::input::Action::MoveDown));
  CHECK_FALSE(prompt.confirm_selected());
  prompt.step(with_only(corundum::input::Action::MoveUp));
  CHECK(prompt.confirm_selected());
}

TEST_CASE("TransitionPrompt — Select on Yes confirms and exposes the stashed transition") {
  const Portal portal = make_portal();
  TransitionPrompt prompt{portal};
  const Step result = prompt.step(with_only(corundum::input::Action::Select));
  CHECK(result == Step::Confirmed);
  CHECK(prompt.transition().target_map == portal.target_map);
  CHECK(prompt.transition().spawn_col == portal.spawn_col);
  CHECK(prompt.transition().spawn_row == portal.spawn_row);
  CHECK_FALSE(prompt.transition().return_to_world);
  CHECK_FALSE(prompt.declined());
}

TEST_CASE("TransitionPrompt — Select on No dismisses and latches declined") {
  TransitionPrompt prompt{make_portal()};
  prompt.step(with_only(corundum::input::Action::MoveRight));
  REQUIRE_FALSE(prompt.confirm_selected());

  const Step result = prompt.step(with_only(corundum::input::Action::Select));
  CHECK(result == Step::Dismissed);
  CHECK(prompt.declined());
}

TEST_CASE("TransitionPrompt — Cancel dismisses from any highlight") {
  {
    TransitionPrompt prompt{make_portal()};
    REQUIRE(prompt.confirm_selected());
    const Step result = prompt.step(with_only(corundum::input::Action::Cancel));
    CHECK(result == Step::Dismissed);
    CHECK(prompt.declined());
  }
  {
    TransitionPrompt prompt{make_portal()};
    prompt.step(with_only(corundum::input::Action::MoveRight));
    REQUIRE_FALSE(prompt.confirm_selected());
    const Step result = prompt.step(with_only(corundum::input::Action::Cancel));
    CHECK(result == Step::Dismissed);
    CHECK(prompt.declined());
  }
}

TEST_CASE("TransitionPrompt — no input returns Pending") {
  TransitionPrompt prompt{make_portal()};
  corundum::input::InputState input{};
  CHECK(prompt.step(input) == Step::Pending);
  CHECK(prompt.confirm_selected());
  CHECK_FALSE(prompt.declined());
}

TEST_CASE("TransitionPrompt — overlaps matches the arming portal's AABB") {
  const Portal portal = make_portal(); // col=13, row=13, w=1, h=1 → AABB (13..14, 13..14)
  TransitionPrompt prompt{portal};

  // AABB (col0..col1, row0..row1) per the portal-overlap test convention.
  CHECK(prompt.overlaps(13.5f, 13.5f, 13.5f, 13.5f));      // feet-centered in the cell
  CHECK(prompt.overlaps(12.5f, 13.5f, 13.5f, 13.5f));      // straddling left edge
  CHECK_FALSE(prompt.overlaps(10.f, 10.5f, 13.5f, 13.5f)); // clear to the left
  CHECK_FALSE(prompt.overlaps(13.5f, 13.5f, 11.f, 11.5f)); // clear above
}

TEST_CASE("TransitionPrompt — guards matches the arming portal exactly") {
  const Portal portal = make_portal();
  TransitionPrompt prompt{portal};
  CHECK(prompt.guards(portal));

  Portal shifted = portal;
  shifted.col = 14.f;
  CHECK_FALSE(prompt.guards(shifted));

  Portal wider = portal;
  wider.w = 2.f;
  CHECK_FALSE(prompt.guards(wider));
}
