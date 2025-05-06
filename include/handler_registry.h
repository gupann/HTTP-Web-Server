#pragma once
#include <memory>
#include <string>
#include <vector>
#include "config_parser.h"
#include "request_handler.h"

// ------------------------------------------------------------------
// HandlerRegistry
// ------------------------------------------------------------------
// * Reads   <url_prefix>  <HandlerName> { ... }   blocks from the
//   already-parsed NginxConfig tree.
// * Creates ONE handler instance per block and keeps it around.
// * Chooses a handler at request time via longest-prefix match.
// ------------------------------------------------------------------
class HandlerRegistry {
public:
  bool Init(const NginxConfig &config);                 // build mappings
  request_handler *Match(const std::string &uri) const; // look-up

private:
  struct Mapping {
    std::string prefix;                       // e.g. "/static"
    std::unique_ptr<request_handler> handler; // EchoHandler, StaticHandler…
  };
  std::vector<Mapping> mappings_; // sorted longest→shortest
};
