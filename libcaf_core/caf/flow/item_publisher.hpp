// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/flow/multicaster.hpp"

namespace caf::flow {

template <class T>
using item_publisher [[deprecated("use multicaster instead")]] = multicaster<T>;

} // namespace caf::flow
