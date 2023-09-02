// This example an HTTP server that implements a REST API by forwarding requests
// to an actor. The actor in this example is a simple key-value store. The actor
// is not aware of HTTP and the HTTP server is sending regular request messages
// to actor.

#include "caf/net/http/with.hpp"
#include "caf/net/middleman.hpp"

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/caf_main.hpp"
#include "caf/deep_to_string.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/scheduled_actor/flow.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <utility>

namespace http = caf::net::http;

// -- constants ----------------------------------------------------------------

static constexpr uint16_t default_port = 8080;

static constexpr size_t default_max_connections = 128;

// -- configuration ------------------------------------------------------------

struct config : caf::actor_system_config {
  config() {
    opt_group{custom_options_, "global"} //
      .add<uint16_t>("port,p", "port to listen for incoming connections")
      .add<size_t>("max-connections,m", "limit for concurrent clients");
    opt_group{custom_options_, "tls"} //
      .add<std::string>("key-file,k", "path to the private key file")
      .add<std::string>("cert-file,c", "path to the certificate file");
  }
};

// -- our key-value store actor ------------------------------------------------

struct kvs_actor_state {
  caf::behavior make_behavior() {
    using caf::result;
    return {
      [this](caf::get_atom, const std::string& key) -> result<std::string> {
        if (auto i = data.find(key); i != data.end())
          return i->second;
        else
          return make_error(caf::sec::no_such_key, key + " not found");
      },
      [this](caf::put_atom, const std::string& key, std::string& value) {
        data.insert_or_assign(key, std::move(value));
      },
      [this](caf::delete_atom, const std::string& key) { data.erase(key); },
    };
  }

  std::map<std::string, std::string> data;
};

using kvs_actor_impl = caf::stateful_actor<kvs_actor_state>;

// -- utility functions --------------------------------------------------------

bool is_ascii(caf::span<const std::byte> buffer) {
  auto pred = [](auto x) { return isascii(static_cast<unsigned char>(x)); };
  return std::all_of(buffer.begin(), buffer.end(), pred);
}

std::string to_ascii(caf::span<const std::byte> buffer) {
  return std::string{reinterpret_cast<const char*>(buffer.data()),
                     buffer.size()};
}

// -- main ---------------------------------------------------------------------

int caf_main(caf::actor_system& sys, const config& cfg) {
  using namespace std::literals;
  namespace ssl = caf::net::ssl;
  // Read the configuration.
  auto port = caf::get_or(cfg, "port", default_port);
  auto pem = ssl::format::pem;
  auto key_file = caf::get_as<std::string>(cfg, "tls.key-file");
  auto cert_file = caf::get_as<std::string>(cfg, "tls.cert-file");
  auto max_connections = caf::get_or(cfg, "max-connections",
                                     default_max_connections);
  if (!key_file != !cert_file) {
    std::cerr << "*** inconsistent TLS config: declare neither file or both\n";
    return EXIT_FAILURE;
  }
  // Spin up our key-value store actor.
  auto kvs = sys.spawn<kvs_actor_impl>();
  // Open up a TCP port for incoming connections and start the server.
  auto server
    = http::with(sys)
        // Optionally enable TLS.
        .context(ssl::context::enable(key_file && cert_file)
                   .and_then(ssl::emplace_server(ssl::tls::v1_2))
                   .and_then(ssl::use_private_key_file(key_file, pem))
                   .and_then(ssl::use_certificate_file(cert_file, pem)))
        // Bind to the user-defined port.
        .accept(port)
        // Limit how many clients may be connected at any given time.
        .max_connections(max_connections)
        // Stop the server if our key-value store actor terminates.
        .monitor(kvs)
        // Forward incoming requests to the kvs actor.
        .route("/api/<arg>", http::method::get,
               [kvs](http::responder& res, std::string key) {
                 auto* self = res.self();
                 auto prom = std::move(res).to_promise();
                 self->request(kvs, 2s, caf::get_atom_v, std::move(key))
                   .then(
                     [prom](const std::string& value) mutable {
                       prom.respond(http::status::ok, "text/plain", value);
                     },
                     [prom](const caf::error& what) mutable {
                       if (what == caf::sec::no_such_key)
                         prom.respond(http::status::not_found, "text/plain",
                                      "Key not found.");
                       else
                         prom.respond(http::status::internal_server_error,
                                      what);
                     });
               })
        .route("/api/<arg>", http::method::post,
               [kvs](http::responder& res, std::string key) {
                 auto value = res.payload();
                 if (!is_ascii(value)) {
                   res.respond(http::status::bad_request, "text/plain",
                               "Expected an ASCII payload.");
                   return;
                 }
                 auto* self = res.self();
                 auto prom = std::move(res).to_promise();
                 self
                   ->request(kvs, 2s, caf::put_atom_v, std::move(key),
                             to_ascii(value))
                   .then(
                     [prom]() mutable {
                       prom.respond(http::status::no_content);
                     },
                     [prom](const caf::error& what) mutable {
                       prom.respond(http::status::internal_server_error, what);
                     });
               })
        .route("/api/<arg>", http::method::del,
               [kvs](http::responder& res, std::string key) {
                 auto* self = res.self();
                 auto prom = std::move(res).to_promise();
                 self->request(kvs, 2s, caf::delete_atom_v, std::move(key))
                   .then(
                     [prom]() mutable {
                       prom.respond(http::status::no_content);
                     },
                     [prom](const caf::error& what) mutable {
                       prom.respond(http::status::internal_server_error, what);
                     });
               })
        .route("/status", http::method::get,
               [kvs](http::responder& res) {
                 res.respond(http::status::no_content);
               })
        // Launch the server.
        .start();
  // Report any error to the user.
  if (!server) {
    std::cerr << "*** unable to run at port " << port << ": "
              << to_string(server.error()) << '\n';
    return EXIT_FAILURE;
  }
  // Note: the actor system will keep the application running for as long as the
  // kvs actor stays alive.
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::net::middleman)
