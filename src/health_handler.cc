#include "health_handler.h"
#include "handler_factory.h"

using namespace wasd::http;
namespace http = boost::beast::http;

// ------------------------------------------------------------------
// build 200-OK response with plain-text "OK"
// ------------------------------------------------------------------
std::unique_ptr<Response> health_handler::handle_request(const Request &req) {
  auto res = std::make_unique<Response>();
  res->version(req.version());
  res->result(http::status::ok);
  res->set(http::field::content_type, "text/plain");
  res->body() = "OK";
  res->prepare_payload();
  return res;
}

// self-registration archetype
REGISTER_HANDLER("HealthRequestHandler", health_handler)
