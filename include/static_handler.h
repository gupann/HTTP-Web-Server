#ifndef STATIC_HANDLER_H
#define STATIC_HANDLER_H

#include "request_handler.h"

namespace wasd::http {

namespace http = boost::beast::http;

class static_handler : public request_handler {
public:
  using request_handler::request_handler;
  void handle_request(http::request<http::string_body> &req,
                      http::response<http::string_body> &res) override;
};

} // namespace wasd::http

#endif
