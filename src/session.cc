#include "session.h"
#include <boost/log/trivial.hpp>
#include "echo_handler.h"
#include "static_handler.h"

using Clock = std::chrono::steady_clock;
using namespace wasd::http;
namespace http = boost::beast::http;

session::session(boost::asio::io_service &io, std::shared_ptr<HandlerRegistry> reg)
    : socket_(io), registry_(std::move(reg)) {}

session::~session() = default;

tcp::socket &session::socket() {
  return socket_;
}

void session::start() {
  start_time_ = Clock::now();
  http::async_read(
      socket_, buffer_, req_,
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
    delete this;
    return;
  }
  if (error) {
    delete this;
    return;
  }

  // 1) Try to match the URI; 2) if none, fall back to echo
  HandlerFactory *fac = registry_->Match(std::string(req_.target()));
  std::unique_ptr<RequestHandler> handler;
  if (fac) {
    handler = (*fac)();
  } else {
    handler = std::make_unique<echo_handler>(std::string());
  }

  // 3) Generate the response
  res_ = handler->handle_request(req_);

  // 4) Send it
  http::async_write(socket_, *res_, [this](const boost::system::error_code &ec, std::size_t bytes) {
    handle_write(ec, bytes);
  });
}

void session::handle_write(const boost::system::error_code &error,
                           std::size_t /*bytes_transferred*/) {
  // On error, tear down immediately
  if (error) {
    delete this;
    return;
  }

  // Keep-alive? loop. Otherwise close.
  if (req_.keep_alive()) {
    start();
  } else {
    delete this;
  }
}
