#pragma once

#include <memory>            // For std::unique_ptr
#include "request_handler.h" // For RequestHandler, Request, Response

namespace wasd::http {

class not_found_handler : public RequestHandler {
public:
  not_found_handler(); // Default constructor
  ~not_found_handler() override = default;

  std::unique_ptr<Response> handle_request(const Request &req) override;
};

} // namespace wasd::http
