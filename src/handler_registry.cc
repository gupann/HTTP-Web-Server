#include "handler_registry.h"
#include "static_handler.h"
#include "echo_handler.h"
#include <boost/log/trivial.hpp>
#include <algorithm>

// Helper: create the correct concrete handler
static std::unique_ptr<request_handler>
MakeHandler(const std::string& type,
            const std::string& prefix,
            const NginxConfig* child_block)
{
  if (type == "StaticHandler") {
    // expect: root <dir>;
    std::string root_dir = "/";
    if (child_block) {
      for (const auto& stmt : child_block->statements_) {
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
bool HandlerRegistry::Init(const NginxConfig& config)
{
  for (const auto& stmt : config.statements_) {
    if (stmt->tokens_.size() < 3) continue;       // need at least: location <path> <HandlerName>

    // recognise only blocks that start with the keyword "location"
    if (stmt->tokens_[0] != "location") continue;

    const std::string& prefix = stmt->tokens_[1];   // e.g. /static1
    const std::string& type   = stmt->tokens_[2];   // e.g. StaticHandler

    auto h = MakeHandler(type, prefix, stmt->child_block_.get());
    if (!h) return false;

    mappings_.push_back({prefix, std::move(h)});
  }

  // longest-prefix match → sort by descending prefix length
  std::sort(mappings_.begin(), mappings_.end(),
            [](const Mapping& a, const Mapping& b) {
              return a.prefix.size() > b.prefix.size();
            });

  return true;
}

request_handler* HandlerRegistry::Match(const std::string& uri) const
{
  for (const auto& m : mappings_) {
    if (uri.rfind(m.prefix, 0) == 0)      // prefix at pos 0
      return m.handler.get();
  }
  return nullptr;
}
