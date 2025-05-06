#include "session.h"
#include <boost/bind.hpp>
#include <boost/log/trivial.hpp>
#include <iostream> // for server debugging output
#include <sstream>
#include "echo_handler.h"
#include "static_handler.h"
using Clock = std::chrono::steady_clock; // alias

namespace http = boost::beast::http;

session::session(boost::asio::io_service &io_service, std::shared_ptr<HandlerRegistry> registry)
    : socket_(io_service), registry_(std::move(registry)) {}

// Define virtual constructor
session::~session() = default;

tcp::socket &session::socket() {
  return socket_;
}

void session::start() {
  start_time_ = Clock::now();

  http::async_read(socket_, buffer_, req_,
                   boost::bind(&session::handle_read, this, boost::asio::placeholders::error,
                               boost::asio::placeholders::bytes_transferred));
}

void session::call_handle_read(const boost::system::error_code &error, size_t bytes_transferred) {
  handle_read(error, bytes_transferred);
}

http::response<http::string_body> session::get_response() {
  return res_;
}

void session::set_request(http::request<http::string_body> req) {
  req_ = req;
}

void session::handle_read(const boost::system::error_code &error, std::size_t bytes_transferred) {

  if (error == http::error::end_of_stream) {
    std::string ip = "unknown";
    try {
      ip = socket_.remote_endpoint().address().to_string();
    } catch (...) {
    }
    BOOST_LOG_TRIVIAL(info) << ip << " Connection closed by client (EOF)";
    delete this;
    return;
  }

  if (error) {
    std::string ip = "unknown";
    try {
      ip = socket_.remote_endpoint().address().to_string();
    } catch (...) {
    }
    BOOST_LOG_TRIVIAL(error) << ip << " handle_read error: " << error.message();
    delete this;
    return;
  }

  request_handler *h = registry_->Match(std::string(req_.target()));
  if (!h) {
    static echo_handler default_echo("/", "");
    h = &default_echo;
  }
  h->handle_request(req_, res_);

  http::async_write(socket_, res_,
                    boost::bind(&session::handle_write, this, boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
}

void session::handle_write(const boost::system::error_code &error, std::size_t bytes_transferred) {
  // compute latency
  auto latency_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time_).count();

  if (error) {
    BOOST_LOG_TRIVIAL(error) << "write error: " << error.message();
    delete this;
    return;
  }

  // structured per request log
  try {
    auto ip = socket_.remote_endpoint().address().to_string();
    BOOST_LOG_TRIVIAL(info) << ip << " " << req_.method_string() << " " << req_.target() << " => "
                            << res_.result_int() << " " << latency_ms << "ms " << bytes_transferred
                            << "B";
  } catch (...) {
    BOOST_LOG_TRIVIAL(warning) << "Could not log remote endpoint";
  }

  if (req_.keep_alive()) {
    start();
  } else {
    BOOST_LOG_TRIVIAL(info) << "Connection closed by client";
    delete this;
  }
}