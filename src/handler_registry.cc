#include "handler_registry.h"
#include <algorithm>
#include <boost/log/trivial.hpp>
#include "echo_handler.h"
#include "static_handler.h"

using namespace wasd::http;

// Helper: create the correct concrete handler
static std::unique_ptr<request_handler>
MakeHandler(const std::string &type, const std::string &prefix, const NginxConfig *child_block) {
  if (type == "StaticHandler") {
    // expect: root <dir>;
    std::string root_dir = "/";
    if (child_block) {
      for (const auto &stmt : child_block->statements_) {
        if (stmt->tokens_.size() == 2 && stmt->tokens_[0] == "root") {
          root_dir = stmt->tokens_[1];
        }
      }
    }
    return std::make_unique<static_handler>(prefix, root_dir);
  }

  if (type == "EchoHandler") {
    return std::make_unique<echo_handler>(prefix, "");
  }

  BOOST_LOG_TRIVIAL(error) << "Unknown handler type '" << type << "'";
  return nullptr;
}

// ------------------------------------------------------------------
// HandlerRegistry implementation
// ------------------------------------------------------------------
bool HandlerRegistry::Init(const NginxConfig &config) {
  for (const auto &stmt : config.statements_) {
    if (stmt->tokens_.size() < 3)
      continue; // need at least: location <path> <HandlerName>

    // recognise only blocks that start with the keyword "location"
    if (stmt->tokens_[0] != "location")
      continue;

    const std::string &prefix = stmt->tokens_[1]; // e.g. /static1
    const std::string &type = stmt->tokens_[2];   // e.g. StaticHandler

    // Enforce that a child block must exist for handler definitions
    if (!stmt->child_block_) {
      BOOST_LOG_TRIVIAL(error) << "Configuration error for handler type '" << type
                               << "' at location '" << prefix
                               << "': Missing required '{...}' block.";
      return false; // Fail initialization
    }

    // DO NOT USE UNTIL CONFIG PARSER HAS BEEN DISCUSSED
    // // Validate "The presence of quoting around strings (e.g. the serving path) is not
    // supported." if (prefix.length() >= 2 && ((prefix.front() == '"' && prefix.back() == '"') ||
    //                              (prefix.front() == '\'' && prefix.back() == '\''))) {
    //   BOOST_LOG_TRIVIAL(error) << "Configuration error for handler type '" << type
    //                            << "' at location " << prefix // Log the token with quotes
    //                            << ": Serving path must not be enclosed in quotes.";
    //   return false; // Fail initialization
    // }

    // Validate that paths must start with '/'
    if (prefix.empty() || prefix.front() != '/') {
      BOOST_LOG_TRIVIAL(error) << "Configuration error for handler type '" << type
                               << "' at location '" << prefix
                               << "': Serving path must start with '/'.";
      return false; // Fail initialization
    }

    // Validate "Trailing slashes on URL serving paths are prohibited."
    // We allow "/" as a valid root path, but any other path like "/foo/" is invalid.
    if (prefix.length() > 1 && prefix.back() == '/') {
      BOOST_LOG_TRIVIAL(error) << "Configuration error for handler type '" << type
                               << "' at location '" << prefix
                               << "': Serving path must not have a trailing slash.";
      return false; // Fail initialization
    }

    // Validate "Duplicate locations in the config should result in the server failing at startup."
    for (const auto &existing_mapping : mappings_) {
      if (existing_mapping.prefix == prefix) {
        BOOST_LOG_TRIVIAL(error)
            << "Configuration error: Duplicate location prefix '" << prefix
            << "' defined for handler type '" << type << "'. Previously defined for handler type '"
            << (existing_mapping.handler
                    ? "some_handler_type"
                    : "unknown") // Placeholder, ideally get type from existing handler
            << "'.";
        // To get the actual type of the existing handler, you might need to add a way for handlers
        // to report their type, or store the type string alongside the handler in the Mapping
        // struct. For now, a generic message is used.
        return false; // Fail initialization
      }
    }

    auto h = MakeHandler(type, prefix, stmt->child_block_.get());
    if (!h)
      return false;

    mappings_.push_back({prefix, std::move(h)});
  }

  // longest-prefix match → sort by descending prefix length
  std::sort(mappings_.begin(), mappings_.end(),
            [](const Mapping &a, const Mapping &b) { return a.prefix.size() > b.prefix.size(); });

  return true;
}

request_handler *HandlerRegistry::Match(const std::string &uri) const {
  for (const auto &m : mappings_) {
    if (uri.rfind(m.prefix, 0) == 0) // prefix at pos 0
      return m.handler.get();
  }
  return nullptr;
}
