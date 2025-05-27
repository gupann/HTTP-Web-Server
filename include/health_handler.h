#pragma once

#include <memory>
#include "request_handler.h"

namespace wasd::http {

class health_handler : public RequestHandler {
public:
  health_handler() = default;

  std::unique_ptr<Response> handle_request(const Request &req) override;
};

} // namespace wasd::http
