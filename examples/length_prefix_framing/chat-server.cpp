// Simple chat server with a binary protocol.

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/caf_main.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/net/lp/with.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/net/tcp_accept_socket.hpp"
#include "caf/net/tcp_stream_socket.hpp"
#include "caf/scheduled_actor/flow.hpp"
#include "caf/uuid.hpp"

#include <iostream>
#include <utility>

// -- convenience type aliases -------------------------------------------------

// The trait for translating between bytes on the wire and flow items. The
// binary default trait operates on binary::frame items.
using trait = caf::net::binary::default_trait;

// An implicitly shared type for storing a binary frame.
using bin_frame = caf::net::binary::frame;

// Each client gets a UUID for identifying it. While processing messages, we add
// this ID to the input to tag it.
using message_t = std::pair<caf::uuid, bin_frame>;

// -- constants ----------------------------------------------------------------

static constexpr uint16_t default_port = 7788;

static constexpr size_t default_max_connections = 128;

// -- configuration setup ------------------------------------------------------

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

// -- multiplexing logic -------------------------------------------------------

void worker_impl(caf::event_based_actor* self,
                 trait::acceptor_resource events) {
  // Allows us to push new flows into the central merge point.
  caf::flow::item_publisher<caf::flow::observable<message_t>> msg_pub{self};
  // Our central merge point combines all inputs into a single, shared flow.
  auto messages = msg_pub.as_observable().merge().share();
  // Have one subscription for debug output. This also makes sure that the
  // shared observable stays subscribed to the merger.
  messages.for_each([](const message_t& msg) {
    const auto& [conn, frame] = msg;
    std::cout << "*** got message of size " << frame.size() << " from "
              << to_string(conn) << '\n';
  });
  // Connect the flows for each incoming connection.
  events
    .observe_on(self) //
    .for_each(
      [self, messages, pub = std::move(msg_pub)] //
      (const trait::accept_event& event) mutable {
        // Each connection gets a unique ID.
        auto conn = caf::uuid::random();
        std::cout << "*** accepted new connection " << to_string(conn) << '\n';
        auto& [pull, push] = event.data();
        // Subscribe the `push` end to the central merge point.
        messages
          .filter([conn](const message_t& msg) {
            // Drop all messages received by this conn.
            return msg.first != conn;
          })
          .map([](const message_t& msg) {
            // Remove the server-internal UUID.
            return msg.second;
          })
          .subscribe(push);
        // Feed messages from the `pull` end into the central merge point.
        auto inputs
          = pull.observe_on(self)
              .do_on_error([](const caf::error& err) {
                std::cout << "*** connection error: " << to_string(err) << '\n';
              })
              .on_error_complete() // Cary on if a connection breaks.
              .do_on_complete([conn] {
                std::cout << "*** lost connection " << to_string(conn) << '\n';
              })
              .map([conn](const bin_frame& frame) {
                return message_t{conn, frame};
              })
              .as_observable();
        pub.push(inputs);
      });
}

// -- main ---------------------------------------------------------------------

int caf_main(caf::actor_system& sys, const config& cfg) {
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
  // Open up a TCP port for incoming connections and start the server.
  auto server
    = caf::net::lp::with(sys)
        // Optionally enable TLS.
        .context(ssl::context::enable(key_file && cert_file)
                   .and_then(ssl::emplace_server(ssl::tls::v1_2))
                   .and_then(ssl::use_private_key_file(key_file, pem))
                   .and_then(ssl::use_certificate_file(cert_file, pem)))
        // Bind to the user-defined port.
        .accept(port)
        // Limit how many clients may be connected at any given time.
        .max_connections(max_connections)
        // When started, run our worker actor to handle incoming connections.
        .start([&sys](trait::acceptor_resource accept_events) {
          sys.spawn(worker_impl, std::move(accept_events));
        });
  // Report any error to the user.
  if (!server) {
    std::cerr << "*** unable to run at port " << port << ": "
              << to_string(server.error()) << '\n';
    return EXIT_FAILURE;
  }
  // Note: the actor system will keep the application running for as long as the
  // workers are still alive.
  return EXIT_SUCCESS;
}

CAF_MAIN(caf::net::middleman)
