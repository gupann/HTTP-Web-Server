#include "session.h"
#include <boost/log/trivial.hpp>
#include "echo_handler.h"
#include "static_handler.h"

using Clock = std::chrono::steady_clock;
using namespace wasd::http;
namespace http = boost::beast::http;

session::session(boost::asio::io_service &io, std::shared_ptr<HandlerRegistry> reg)
    : socket_(io), registry_(std::move(reg)) {}

// Define virtual constructor
session::~session() = default;

tcp::socket &session::socket() {
  return socket_;
}

void session::start() {
  start_time_ = Clock::now();

  http::async_read(
      socket_, buffer_, req_,
      // capture raw this - server doesn’t own shared_ptr
      [this](const boost::system::error_code &ec, std::size_t bytes) { handle_read(ec, bytes); });
}

static std::string safe_endpoint(const tcp::socket &sock) {
  try {
    return sock.remote_endpoint().address().to_string();
  } catch (...) {
    return "unknown";
  }
}

void session::handle_read(const boost::system::error_code &error,
                          std::size_t /*bytes_transferred*/) {
  if (error == http::error::end_of_stream) {
    BOOST_LOG_TRIVIAL(info) << safe_endpoint(socket_) << " EOF";
    delete this;
    return;
  }
  if (error) {
    BOOST_LOG_TRIVIAL(error) << safe_endpoint(socket_) << " read error: " << error.message();
    delete this;
    return;
  }

  request_handler *h = registry_->Match(std::string(req_.target()));
  if (!h) {
    static echo_handler default_echo("/", "");
    h = &default_echo; // default echo if no match
  }
  h->handle_request(req_, res_);

  http::async_write(socket_, res_, [this](const boost::system::error_code &ec, std::size_t bytes) {
    handle_write(ec, bytes);
  });
}

void session::handle_write(const boost::system::error_code &error, std::size_t bytes_transferred) {
  // compute latency
  auto latency =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time_).count();

  if (error) {
    BOOST_LOG_TRIVIAL(error) << "write error: " << error.message();
    delete this;
    return;
  }

  BOOST_LOG_TRIVIAL(info) << safe_endpoint(socket_) << ' ' << req_.method_string() << ' '
                          << req_.target() << " => " << res_.result_int() << ' ' << latency << "ms "
                          << bytes_transferred << 'B';

  if (req_.keep_alive()) {
    start();
  } else {
    BOOST_LOG_TRIVIAL(info) << "Connection closed by client";
    delete this;
  }
}