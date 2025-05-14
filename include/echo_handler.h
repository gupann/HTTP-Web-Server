#ifndef ECHO_HANDLER_H
#define ECHO_HANDLER_H

#include <boost/beast.hpp>
#include <memory>
#include <string>
#include "handler_factory.h" // REGISTER_HANDLER
#include "request_handler.h" // Request / Response aliases

namespace wasd::http {

class echo_handler : public RequestHandler {
public:
  echo_handler();
  // Common‑API ctor: only the serving prefix is captured
  explicit echo_handler(std::string prefix);

  // Build & return the HTTP response
  std::unique_ptr<Response> handle_request(const Request &req) override;

private:
  std::string prefix_;
};

} // namespace wasd::http

#endif // ECHO_HANDLER_H
