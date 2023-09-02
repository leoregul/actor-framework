// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#define CAF_SUITE flow.multicaster

#include "caf/flow/multicaster.hpp"

#include "caf/flow/observable_builder.hpp"
#include "caf/flow/op/merge.hpp"
#include "caf/flow/scoped_coordinator.hpp"

#include "core-test.hpp"

using namespace caf;

namespace {

struct fixture : test_coordinator_fixture<> {
  flow::scoped_coordinator_ptr ctx = flow::make_scoped_coordinator();

  template <class... Ts>
  std::vector<int> ls(Ts... xs) {
    return std::vector<int>{xs...};
  }
};

} // namespace

BEGIN_FIXTURE_SCOPE(fixture)

SCENARIO("a multicaster discards items that arrive before a subscriber") {
  GIVEN("an multicaster") {
    WHEN("pushing items") {
      THEN("observers see only items that were pushed after subscribing") {
        auto uut = flow::multicaster<int>{ctx.get()};
        uut.push({1, 2, 3});
        auto snk = flow::make_auto_observer<int>();
        uut.subscribe(snk->as_observer());
        ctx->run();
        uut.push({4, 5, 6});
        ctx->run();
        uut.close();
        CHECK_EQ(snk->buf, ls(4, 5, 6));
        CHECK_EQ(snk->state, flow::observer_state::completed);
      }
    }
  }
}

END_FIXTURE_SCOPE()
