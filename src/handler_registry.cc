#include "handler_registry.h"

#include <algorithm>
#include <boost/log/trivial.hpp>

#include "echo_handler.h"
#include "handler_factory.h" // Instance(), Lookup(), …
#include "static_handler.h"  // only needed temporarily for argument parsing helpers

using namespace wasd::http;

static bool ParseStaticBlock(const NginxConfig *block, std::string *root_out) {
  if (!block)
    return false;
  for (const auto &stmt : block->statements_) {
    if (stmt->tokens_.size() == 2 && stmt->tokens_[0] == "root") {
      *root_out = stmt->tokens_[1];
      return true;
    }
  }
  return false; // root not found
}

// ------------------------------------------------------------------
// HandlerRegistry::Init
// ------------------------------------------------------------------
bool HandlerRegistry::Init(const NginxConfig &config) {
  for (const auto &stmt : config.statements_) {
    if (stmt->tokens_.size() < 3 || stmt->tokens_[0] != "location")
      continue;

    // Enforce that every 'location ... Handler' has a BLOCK `{ ... }`
    if (!stmt->child_block_) {
      BOOST_LOG_TRIVIAL(error) << "Missing block `{}` for handler definition at location "
                               << stmt->tokens_[1] << " " << stmt->tokens_[2];
      return false;
    }
    const std::string &prefix = stmt->tokens_[1]; // "/foo"
    const std::string &type = stmt->tokens_[2];   // "StaticHandler"

    // Basic validation (trailing slash, dupes, etc.) – keep what you already had
    if (prefix.empty() || prefix[0] != '/') {
      BOOST_LOG_TRIVIAL(error) << "Path must start with '/': " << prefix;
      return false;
    }
    if (prefix.size() > 1 && prefix.back() == '/') {
      BOOST_LOG_TRIVIAL(error) << "Path must not end with '/': " << prefix;
      return false;
    }
    for (const auto &m : mappings_) {
      if (m.prefix == prefix) {
        BOOST_LOG_TRIVIAL(error) << "Duplicate location: " << prefix;
        return false;
      }
    }

    // ----------------------------------------------------------------
    // Look up factory template for <type>
    // ----------------------------------------------------------------
    HandlerFactory *archetype = HandlerFactoryRegistry::Instance().Lookup(type);
    if (!archetype) {
      BOOST_LOG_TRIVIAL(error) << "Unknown handler type '" << type << "'";
      return false;
    }

    HandlerFactory bound_factory;

    if (type == "StaticHandler") {
      std::string root_dir = ".";
      if (!ParseStaticBlock(stmt->child_block_.get(), &root_dir)) {
        BOOST_LOG_TRIVIAL(error) << "StaticHandler at " << prefix
                                 << " missing/invalid root directive";
        return false;
      }
      bound_factory = [prefix, root_dir]() {
        return std::make_unique<static_handler>(prefix, root_dir);
      };
    } else if (type == "EchoHandler") {
      bound_factory = [prefix]() { return std::make_unique<echo_handler>(prefix); };
    } else {
      // Generic fallback: zero-arg constructor via REGISTER_HANDLER
      bound_factory = *archetype;
    }
    mappings_.push_back({prefix, std::move(bound_factory)});
  }

  // sort longest‑prefix first
  std::sort(mappings_.begin(), mappings_.end(),
            [](const Mapping &a, const Mapping &b) { return a.prefix.size() > b.prefix.size(); });
  return true;
}

// ------------------------------------------------------------------
// HandlerRegistry::Match
// ------------------------------------------------------------------
HandlerFactory *HandlerRegistry::Match(const std::string &uri) const {
  for (const auto &m : mappings_) {
    if (uri.rfind(m.prefix, 0) == 0) { // prefix match at pos 0
      return const_cast<HandlerFactory *>(&m.factory);
    }
  }
  return nullptr;
}
