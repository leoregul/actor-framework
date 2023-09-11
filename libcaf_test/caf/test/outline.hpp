// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/test/context.hpp"
#include "caf/test/scenario.hpp"

namespace caf::test {

class outline : public runnable {
public:
  using super = runnable;

  class examples_setter {
  public:
    using examples_t = std::vector<std::map<std::string, std::string>>;

    explicit examples_setter(examples_t* examples) : examples_(examples) {
      // nop
    }

    examples_setter(const examples_setter&) = default;

    examples_setter& operator=(const examples_setter&) = default;

    examples_setter& operator=(std::string_view str);

  private:
    examples_t* examples_;
  };

  using super::super;

  void run() override;

  auto make_examples_setter() {
    if (ctx_->example_parameters.empty())
      return examples_setter{&ctx_->example_parameters};
    else
      return examples_setter{nullptr};
  }
};

} // namespace caf::test

#define OUTLINE(description)                                                   \
  struct CAF_PP_UNIFYN(outline_)                                               \
    : caf::test::outline, caf_test_case_auto_fixture {                         \
    using super = caf::test::outline;                                          \
    using super::super;                                                        \
    void do_run() override;                                                    \
    static ptrdiff_t register_id;                                              \
  };                                                                           \
  ptrdiff_t CAF_PP_UNIFYN(outline_)::register_id                               \
    = caf::test::registry::add<CAF_PP_UNIFYN(outline_)>(                       \
      caf_test_suite_name, description, caf::test::block_type::scenario);      \
  void CAF_PP_UNIFYN(outline_)::do_run()

#define EXAMPLES this->make_examples_setter()
