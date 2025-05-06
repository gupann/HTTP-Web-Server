#include "echo_handler.h"

namespace http = boost::beast::http;

void echo_handler::handle_request(http::request<http::string_body> &req,
                                  http::response<http::string_body> &res) {
  std::ostringstream oss;
  oss << req;

  res.set(http::field::content_type, "text/plain");
  res.version(req.version());
  res.result(http::status::ok);
  res.body() = oss.str();
  res.prepare_payload(); // for auto encoding and byte size etc.
}
