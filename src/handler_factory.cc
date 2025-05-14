#include "handler_factory.h"

namespace wasd::http {

HandlerFactoryRegistry &HandlerFactoryRegistry::Instance() {
  static HandlerFactoryRegistry inst;
  return inst;
}

bool HandlerFactoryRegistry::Register(const std::string &name, HandlerFactory f) {
  return map_.emplace(name, std::move(f)).second; // false if duplicate
}

HandlerFactory *HandlerFactoryRegistry::Lookup(const std::string &name) {
  auto it = map_.find(name);
  return it == map_.end() ? nullptr : &it->second;
}

} // namespace wasd::http
