#ifndef HANDLER_FACTORY_H
#define HANDLER_FACTORY_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "request_handler.h"

namespace wasd::http {

using HandlerFactory = std::function<std::unique_ptr<RequestHandler>()>;

class HandlerFactoryRegistry {
public:
  static HandlerFactoryRegistry &Instance();

  // Returns false on name collision
  bool Register(const std::string &type, HandlerFactory f);

  // nullptr if not found
  HandlerFactory *Lookup(const std::string &type);

private:
  std::unordered_map<std::string, HandlerFactory> map_;
};

} // namespace wasd::http

// ------------------------------------------------------------------
// Convenience macro for handler self‑registration
#define REGISTER_HANDLER(TYPE, CLASS, ...)                                                         \
  namespace {                                                                                      \
  const bool registered_##CLASS = wasd::http::HandlerFactoryRegistry::Instance().Register(         \
      TYPE, [__VA_ARGS__]() { return std::make_unique<CLASS>(__VA_ARGS__); });                     \
  }

#endif // HANDLER_FACTORY_H
