// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/handle.hpp>

#include <doctest/doctest.h>

#include <memory>
#include <utility>

using corundum::platform::BackendDeleter;
using corundum::platform::Handle;

namespace {

  /// Counts destructions; lives in the test because the object under test is gone
  /// by the time the count is read.
  struct DestroyTally {
    int count = 0;
  };

  /// Stands in for a platform interface: abstract, virtual destructor, deleted
  /// through the base by the backend-supplied function.
  class ProbeInterface {
  public:
    virtual ~ProbeInterface() = default;

    [[nodiscard]] virtual int identity() const = 0;
  };

  class Probe final : public ProbeInterface {
  public:
    explicit Probe(DestroyTally &tally) : tally_(&tally) {}

    ~Probe() override {
      ++tally_->count;
    }

    Probe(const Probe &) = delete;
    Probe &operator=(const Probe &) = delete;
    Probe(Probe &&) = delete;
    Probe &operator=(Probe &&) = delete;

    [[nodiscard]] int identity() const override {
      return 7;
    }

  private:
    DestroyTally *tally_;
  };

  /// Backend-side destruction, as platform::null::detail::destroy_window does it.
  void destroy_probe(ProbeInterface *object) {
    std::default_delete<ProbeInterface>{}(object);
  }

  Handle<ProbeInterface> adopt(std::unique_ptr<Probe> object) {
    return Handle<ProbeInterface>{object.release(), BackendDeleter<ProbeInterface>{&destroy_probe}};
  }

} // namespace

TEST_CASE("Handle: scope exit destroys the object through the backend-supplied function") {
  DestroyTally tally;

  {
    const Handle<ProbeInterface> handle = adopt(std::make_unique<Probe>(tally));
    CHECK(handle->identity() == 7);
    CHECK(tally.count == 0);
  }

  CHECK(tally.count == 1);
}

TEST_CASE("Handle: reset() destroys immediately and the later scope exit does not repeat it") {
  DestroyTally tally;

  {
    Handle<ProbeInterface> handle = adopt(std::make_unique<Probe>(tally));
    handle.reset();

    CHECK(handle == nullptr);
    CHECK(tally.count == 1);
  }

  CHECK(tally.count == 1);
}

TEST_CASE("Handle: moving hands destruction to the destination") {
  DestroyTally tally;

  {
    Handle<ProbeInterface> source = adopt(std::make_unique<Probe>(tally));
    const Handle<ProbeInterface> destination = std::move(source);

    CHECK(source == nullptr);
    CHECK(tally.count == 0);
  }

  CHECK(tally.count == 1);
}

TEST_CASE("Handle: a default-constructed handle is null and destroys nothing") {
  const DestroyTally tally;

  {
    const Handle<ProbeInterface> handle;

    CHECK(handle == nullptr);
  }

  CHECK(tally.count == 0);
}

TEST_CASE("Handle: the deleter is stored by value and still fires after being copied in") {
  DestroyTally tally;
  const BackendDeleter<ProbeInterface> deleter{&destroy_probe};

  {
    const Handle<ProbeInterface> handle{std::make_unique<Probe>(tally).release(), deleter};

    CHECK(deleter.destroy == &destroy_probe);
    CHECK(tally.count == 0);
  }

  CHECK(tally.count == 1);
}

TEST_CASE("BackendDeleter: a default-constructed deleter carries no function and is inert") {
  const BackendDeleter<ProbeInterface> inert;

  CHECK(inert.destroy == nullptr);

  inert(nullptr); // must not dispatch through a null function pointer
}
