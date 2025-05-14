#include "not_found_handler.h"
#include "handler_factory.h" // For REGISTER_HANDLER

using namespace wasd::http;

namespace http = boost::beast::http;

// Default constructor
not_found_handler::not_found_handler() {}

// Implement the handle_request method as defined in RequestHandler
std::unique_ptr<Response> not_found_handler::handle_request(const Request &req) {
  auto res = std::make_unique<Response>();

  res->set(http::field::content_type, "text/plain");
  res->version(req.version());
  res->result(http::status::not_found);
  res->body() = "404 Not Found";
  res->prepare_payload(); // for auto encoding and byte size etc.
  return res;
}

// Register the handler with the factory.
REGISTER_HANDLER("NotFoundHandler", not_found_handler)