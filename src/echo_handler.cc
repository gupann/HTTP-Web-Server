#include "echo_handler.h"
#include <sstream>
#include "handler_factory.h"

using namespace wasd::http;
namespace http = boost::beast::http;

// ------------------------------------------------------------------
// ctor
// ------------------------------------------------------------------
echo_handler::echo_handler() : prefix_("/") {}
echo_handler::echo_handler(std::string prefix) : prefix_(std::move(prefix)) {}

// ------------------------------------------------------------------
// handle_request – echoes the entire request back in the body
// ------------------------------------------------------------------
std::unique_ptr<Response> echo_handler::handle_request(const Request &req) {
  auto res = std::make_unique<Response>();

  std::ostringstream oss;
  oss << req; // stringify request

  res->set(http::field::content_type, "text/plain");
  res->version(req.version());
  res->result(http::status::ok);
  res->body() = oss.str();
  res->prepare_payload();

  return res;
}

REGISTER_HANDLER("EchoHandler", echo_handler)
