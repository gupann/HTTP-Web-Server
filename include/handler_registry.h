#pragma once
#include <memory>
#include <string>
#include <vector>

#include "config_parser.h"
#include "handler_factory.h"
#include "request_handler.h"

namespace wasd::http {

class HandlerRegistry {
public:
  // Build route table from parsed config; returns false on any error.
  bool Init(const NginxConfig &config);

  // Longest‑prefix match. nullptr if no match.
  HandlerFactory *Match(const std::string &uri) const;

private:
  struct Mapping {
    std::string prefix;     // e.g. "/static"
    HandlerFactory factory; // lambda that builds the handler
  };
  std::vector<Mapping> mappings_; // sorted longest‑>shortest
  HandlerFactory not_found_factory_;
};

} // namespace wasd::http
