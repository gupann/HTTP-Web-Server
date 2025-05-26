#pragma once

#include <chrono>
#include <thread>
#include "handler_factory.h" // REGISTER_HANDLER
#include "request_handler.h" // Request / Response aliases

namespace wasd::http {

class sleep_handler : public RequestHandler {
public:
  sleep_handler() : delay_ms_(3000) {} // default 3-second delay

  // If you ever need a different delay, pass it explicitly (no default here)
  explicit sleep_handler(unsigned delay_ms) : delay_ms_(delay_ms) {}

  std::unique_ptr<Response> handle_request(const Request &req) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    auto res = std::make_unique<Response>();
    res->result(boost::beast::http::status::ok);
    res->set(boost::beast::http::field::content_type, "text/plain");
    res->body() = "Slept";
    res->prepare_payload();
    return res;
  }

private:
  unsigned delay_ms_;
};

} // namespace wasd::http
