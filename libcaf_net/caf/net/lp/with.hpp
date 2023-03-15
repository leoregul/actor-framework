// This file is part of CAF, the C++ Actor Framework. See the file LICENSE in
// the main distribution directory for license terms and copyright or visit
// https://github.com/actor-framework/actor-framework/blob/master/LICENSE.

#pragma once

#include "caf/fwd.hpp"
#include "caf/net/dsl/has_accept.hpp"
#include "caf/net/dsl/has_connect.hpp"
#include "caf/net/dsl/has_context.hpp"
#include "caf/net/lp/client_factory.hpp"
#include "caf/net/lp/server_factory.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/net/ssl/acceptor.hpp"
#include "caf/net/ssl/context.hpp"
#include "caf/net/tcp_accept_socket.hpp"

#include <cstdint>

namespace caf::net::lp {

/// Entry point for the `with(...)` DSL.
template <class Trait>
class with_t : public extend<dsl::base<Trait>, with_t<Trait>>::template //
               with<dsl::has_accept, dsl::has_connect, dsl::has_context> {
public:
  template <class... Ts>
  explicit with_t(multiplexer* mpx, Ts&&... xs)
    : mpx_(mpx), trait_(std::forward<Ts>(xs)...), ctx_(error{}) {
    // nop
  }

  with_t(const with_t&) noexcept = default;

  with_t& operator=(const with_t&) noexcept = default;

  multiplexer* mpx() const noexcept override {
    return mpx_;
  }

  const Trait& trait() const noexcept override {
    return trait_;
  }

  /// @private
  using config_base_type = dsl::config_with_trait<Trait>;

  /// @private
  server_factory<Trait> lift(dsl::server_config_ptr<config_base_type> cfg) {
    return server_factory<Trait>{std::move(cfg)};
  }

  /// @private
  template <class T, class... Ts>
  auto make(dsl::client_config_tag<T> tag, Ts&&... xs) {
    return client_factory<Trait>{tag, std::forward<Ts>(xs)...};
  }

private:
  expected<ssl::context>& get_context_impl() noexcept override {
    return ctx_;
  }

  /// Pointer to multiplexer that runs the protocol stack.
  multiplexer* mpx_;

  /// User-defined trait for configuring serialization.
  Trait trait_;

  /// The optional SSL context.
  expected<ssl::context> ctx_;
};

template <class Trait = binary::default_trait>
with_t<Trait> with(actor_system& sys) {
  return with_t<Trait>{multiplexer::from(sys)};
}

template <class Trait = binary::default_trait>
with_t<Trait> with(multiplexer* mpx) {
  return with_t<Trait>{mpx};
}

} // namespace caf::net::lp
