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
  // Handle parser / network errors
  if (error == http::error::end_of_stream) { // client closed cleanly
    delete this;
    return;
  }

  if (error) { // malformed request, timeout, etc.
    res_ = std::make_unique<Response>();
    res_->version(11); // HTTP/1.1
    res_->result(http::status::bad_request);
    res_->set(http::field::content_type, "text/plain");
    res_->body() = "400 Bad Request";
    res_->prepare_payload();
    res_->keep_alive(false);

    http::async_write(socket_, *res_,
                      [this](const boost::system::error_code &ec, std::size_t bytes) {
                        handle_write(ec, bytes);
                      });
    return; // don’t fall through
  }

  // 1) Attempts to match the URI against our config -- returns 404 handler if no match found
  HandlerFactory *fac = registry_->Match(std::string(req_.target()));
  std::unique_ptr<RequestHandler> handler;
  handler = (*fac)();
  handler_name_ = typeid(*handler).name(); // simple RTTI string

  // 2) Generate the response
  res_ = handler->handle_request(req_);

  // 3) Send it
  http::async_write(socket_, *res_, [this](const boost::system::error_code &ec, std::size_t bytes) {
    handle_write(ec, bytes);
  });
}

void session::handle_write(const boost::system::error_code &error,
                           std::size_t /*bytes_transferred*/) {

  if (res_) { // only log if we actually built a response
    BOOST_LOG_TRIVIAL(info) << "[ResponseMetrics] " << "code:" << res_->result_int() << " "
                            << "path:" << req_.target() << " " << "ip:" << safe_endpoint(socket_)
                            << " " << "handler:" << handler_name_;
  }

  if (error) {
    delete this;
    return;
  }

  // If we already built a response, use its keep-alive flag;
  // otherwise use the flag from the incoming request.
  bool keep = res_ ? res_->keep_alive() : req_.keep_alive();

  if (keep) {
    start(); // stay open: read the next request
  } else {
    delete this; // close connection
  }
}
