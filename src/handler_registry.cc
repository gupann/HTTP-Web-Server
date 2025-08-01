#include "handler_registry.h"
#include <algorithm>
#include <boost/log/trivial.hpp>
#include "handler_factory.h"
#include "handlers/crud_handler.h"
#include "handlers/echo_handler.h"
#include "handlers/health_handler.h"
#include "handlers/markdown_handler.h"
#include "handlers/not_found_handler.h"
#include "handlers/sleep_handler.h"
#include "handlers/static_handler.h"
#include "real_file_system.h"
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
static bool ParseCrudBlock(const NginxConfig *block, std::string *data_path_out) {
  if (!block)
    return false;
  for (const auto &stmt : block->statements_) {
    if (stmt->tokens_.size() == 2 && stmt->tokens_[0] == "data_path") {
      *data_path_out = stmt->tokens_[1];
      return true;
    }
  }
  return false; // data_path not found
}
static bool ParseMarkdownBlock(const NginxConfig *block, std::string *root_out,
                               std::string *template_out) {
  if (!block)
    return false;
  bool root_found = false;
  bool template_found = false;
  for (const auto &stmt : block->statements_) {
    if (stmt->tokens_.size() == 2) {
      if (stmt->tokens_[0] == "root") {
        *root_out = stmt->tokens_[1];
        root_found = true;
      } else if (stmt->tokens_[0] == "template") {
        *template_out = stmt->tokens_[1];
        template_found = true;
      }
    }
  }
  return root_found && template_found; // Require both for successful parsing
}
// ------------------------------------------------------------------
// HandlerRegistry::Init
// ------------------------------------------------------------------
bool HandlerRegistry::Init(const NginxConfig &config) {
  auto real_fs = std::make_shared<RealFileSystem>();
  // Initialize not found handler factory
  not_found_factory_ = []() { return std::make_unique<not_found_handler>(); };
  if (!HandlerFactoryRegistry::Instance().Lookup("SleepHandler")) {
    HandlerFactoryRegistry::Instance().Register("SleepHandler",
                                                []() { return std::make_unique<sleep_handler>(); });
  }
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
    } else if (type == "CrudHandler") {
      std::string data_path = "./data"; // Default data path
      if (!ParseCrudBlock(stmt->child_block_.get(), &data_path)) {
        BOOST_LOG_TRIVIAL(error) << "CrudHandler at " << prefix
                                 << " missing/invalid data_path directive";
        return false;
      }
      bound_factory = [prefix, data_path]() {
        return std::make_unique<CrudRequestHandler>(prefix, data_path);
      };
    } else if (type == "SleepHandler") {
      bound_factory = [prefix]() { return std::make_unique<sleep_handler>(); };
    } else if (type == "HealthRequestHandler") {
      bound_factory = [prefix]() { return std::make_unique<health_handler>(); };
    } else if (type == "MarkdownHandler") {
      std::string md_root;
      std::string md_template;
      if (!ParseMarkdownBlock(stmt->child_block_.get(), &md_root, &md_template)) {
        BOOST_LOG_TRIVIAL(error)
            << "MarkdownHandler at " << prefix
            << " missing or invalid 'root' or 'template' directive in its block.";
        return false;
      }
      bound_factory = [prefix, md_root, md_template, real_fs]() { // <<< Pass real_fs
        return markdown_handler::create(prefix, md_root, md_template, real_fs);
      };
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
    if (uri.rfind(m.prefix, 0) == 0) { // prefix match at pos 0
      return const_cast<HandlerFactory *>(&m.factory);
    }
  }
  // Return the factory for not_found_handler
  return const_cast<HandlerFactory *>(&not_found_factory_);
}