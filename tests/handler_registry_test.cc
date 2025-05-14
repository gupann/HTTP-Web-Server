#include "config_parser.h"
#include "echo_handler.h"
#include "gtest/gtest.h"
#include "handler_registry.h"
#include "static_handler.h"

using namespace wasd::http;

class HandlerRegistryTest : public ::testing::Test {
protected:
  HandlerRegistry registry_;
  NginxConfigParser parser_;
  NginxConfig config_;

  // Helper method to create a config from a string
  bool ParseConfig(const std::string &config_str) {
    std::stringstream ss(config_str);
    return parser_.Parse(&ss, &config_);
  }
};

// Test successful initialization with multiple handlers
TEST_F(HandlerRegistryTest, InitWithMultipleHandlers) {
  std::string config_str = R"(
        location / StaticHandler {
            root ./static;
        }
        location /echo EchoHandler {}
    )";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));

  // Check that the registry has the correct mappings
  HandlerFactory *handler_root = registry_.Match("/");
  ASSERT_NE(handler_root, nullptr);
  auto h_root_1 = (*handler_root)();
  EXPECT_TRUE(dynamic_cast<static_handler *>(h_root_1.get()) != nullptr);

  HandlerFactory *handler_echo = registry_.Match("/echo");
  ASSERT_NE(handler_echo, nullptr);
  auto h_echo = (*handler_echo)();
  EXPECT_TRUE(dynamic_cast<echo_handler *>(h_echo.get()) != nullptr);
}

// Test initialization with no handlers
TEST_F(HandlerRegistryTest, InitWithNoHandlers) {
  std::string config_str = R"()";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));

  // No handlers should be registered
  EXPECT_EQ(registry_.Match("/"), nullptr);
  EXPECT_EQ(registry_.Match("/echo"), nullptr);
}

// Test matching with longest prefix
TEST_F(HandlerRegistryTest, LongestPrefixMatch) {
  std::string config_str = R"(
        location / StaticHandler {
            root ./static;
        }
        location /api EchoHandler {}
        location /api/v1 StaticHandler {
            root ./api_static;
        }
    )";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));

  // Root handler for "/"
  HandlerFactory *root_handler = registry_.Match("/");
  ASSERT_NE(root_handler, nullptr);
  auto h_root_2 = (*root_handler)();
  EXPECT_TRUE(dynamic_cast<static_handler *>(h_root_2.get()) != nullptr);

  // API handler for "/api"
  HandlerFactory *api_handler = registry_.Match("/api");
  ASSERT_NE(api_handler, nullptr);
  auto h_api = (*api_handler)();
  EXPECT_TRUE(dynamic_cast<echo_handler *>(h_api.get()) != nullptr);

  // Specific API v1 handler for "/api/v1"
  HandlerFactory *api_v1_handler = registry_.Match("/api/v1/users");
  ASSERT_NE(api_v1_handler, nullptr);
  auto h_api_v1 = (*api_v1_handler)();
  EXPECT_TRUE(dynamic_cast<static_handler *>(h_api_v1.get()) != nullptr);
}

// Test initialization with invalid handler type
TEST_F(HandlerRegistryTest, InvalidHandlerType) {
  std::string config_str = R"(
        location / UnknownHandler {}
    )";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_FALSE(registry_.Init(config_));
}

// Test matching with subpaths
TEST_F(HandlerRegistryTest, SubpathMatching) {
  std::string config_str = R"(
        location /static StaticHandler {
            root ./static;
        }
    )";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));

  // Exact match
  HandlerFactory *exact_handler = registry_.Match("/static");
  ASSERT_NE(exact_handler, nullptr);
  auto h_exact = (*exact_handler)();
  EXPECT_TRUE(dynamic_cast<static_handler *>(h_exact.get()) != nullptr);

  // Subpath match
  HandlerFactory *subpath_handler = registry_.Match("/static/images");
  ASSERT_NE(subpath_handler, nullptr);
  auto h_subpath = (*subpath_handler)();
  EXPECT_TRUE(dynamic_cast<static_handler *>(h_subpath.get()) != nullptr);
}

// Test handling of root static handler with specific root directory
TEST_F(HandlerRegistryTest, RootHandlerWithSpecificDirectory) {
  std::string config_str = R"(
        location / StaticHandler {
            root ./custom_static;
        }
    )";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));

  HandlerFactory *root_handler = registry_.Match("/");
  ASSERT_NE(root_handler, nullptr);
  auto h_root_3 = (*root_handler)();
  auto *static_handler_ptr = dynamic_cast<static_handler *>(h_root_3.get());
  ASSERT_NE(static_handler_ptr, nullptr);
}

// Test no match returns nullptr
TEST_F(HandlerRegistryTest, NoMatchReturnsNullptr) {
  std::string config_str = R"(
        location /api EchoHandler {}
    )";

  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));

  EXPECT_EQ(registry_.Match("/nonexistent"), nullptr);
}

TEST_F(HandlerRegistryTest, HandlerDefinitionRequiresBlock) {
  std::string config_str = R"(
    port 80;
    location /echo EchoHandler;
  )";
  // Assuming ParseConfig populates this->config_
  ASSERT_TRUE(ParseConfig(config_str));
  // Init should fail because EchoHandler is missing its {} block
  EXPECT_FALSE(registry_.Init(config_));
}

TEST_F(HandlerRegistryTest, HandlerDefinitionWithEmptyBlockIsValid) {
  std::string config_str = R"(
    port 80;
    location /echo EchoHandler {}
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_));
}

TEST_F(HandlerRegistryTest, PathWithTrailingSlashIsInvalid) {
  std::string config_str = R"(
    port 80;
    location /echo/ EchoHandler {}
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_FALSE(registry_.Init(config_))
      << "HandlerRegistry::Init should fail if a serving path has a trailing slash.";
}

TEST_F(HandlerRegistryTest, RootPathWithoutTrailingSlashIsValid) {
  std::string config_str = R"(
    port 80;
    location / StaticHandler { root ./static; }
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_))
      << "HandlerRegistry::Init should succeed for the root path '/'.";
}

TEST_F(HandlerRegistryTest, NonRootPathWithoutTrailingSlashIsValid) {
  std::string config_str = R"(
    port 80;
    location /api EchoHandler {}
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_))
      << "HandlerRegistry::Init should succeed for a non-root path without a trailing slash.";
}

TEST_F(HandlerRegistryTest, PathNotStartingWithSlashIsInvalid) {
  std::string config_str = R"(
    port 80;
    location a StaticHandler { root ./static; }
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_FALSE(registry_.Init(config_))
      << "HandlerRegistry::Init should fail if a serving path does not start with '/'.";
}

TEST_F(HandlerRegistryTest, DuplicateLocationPrefixIsInvalid) {
  std::string config_str = R"(
    port 80;
    location /api EchoHandler {}
    location /api StaticHandler { root ./static; }
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_FALSE(registry_.Init(config_))
      << "HandlerRegistry::Init should fail if a duplicate location prefix is defined.";
}

TEST_F(HandlerRegistryTest, DifferentLocationPrefixesAreValid) {
  std::string config_str = R"(
    port 80;
    location /api EchoHandler {}
    location /api/v2 StaticHandler { root ./static; }
  )";
  ASSERT_TRUE(ParseConfig(config_str));
  EXPECT_TRUE(registry_.Init(config_))
      << "HandlerRegistry::Init should succeed with different location prefixes.";
}

// DO NOT USE UNTIL CONFIG PARSER HAS BEEN DISCUSSED FOR DOUBLE QUOTES

// TEST_F(HandlerRegistryTest, PathWithDoubleQuotesIsInvalid) {
//   std::string config_str = R"(
//     port 80;
//     location "/api" EchoHandler {}
//   )";
//   ASSERT_TRUE(ParseConfig(config_str));
//   EXPECT_FALSE(registry_.Init(config_))
//       << "HandlerRegistry::Init should fail if a serving path is enclosed in double quotes.";
// }

// TEST_F(HandlerRegistryTest, PathWithSingleQuotesIsInvalid) {
//   std::string config_str = R"(
//     port 80;
//     location '/api' EchoHandler {}
//   )";
//   ASSERT_TRUE(ParseConfig(config_str));
//   EXPECT_FALSE(registry_.Init(config_))
//       << "HandlerRegistry::Init should fail if a serving path is enclosed in single quotes.";
// }

// // Modify the existing EmptyPathIsInvalid test
// TEST_F(HandlerRegistryTest, EmptyPathIsInvalid) {
//   std::string config_str_quoted_empty = R"(
//     port 80;
//     location "" StaticHandler { root ./static; }
//   )";
//   // This will now fail due to the "no quotes" rule.
//   ASSERT_TRUE(ParseConfig(config_str_quoted_empty));
//   EXPECT_FALSE(registry_.Init(config_))
//       << "HandlerRegistry::Init should fail for a quoted empty serving path (due to quotes).";

//   // Test for a truly empty token if the parser could produce one (hypothetical for now)
//   // NginxConfigParser likely won't produce an empty unquoted token for `location ;`
//   // but if it did, the `original_path_token.empty()` check in Init would catch it.
//   // For example, if a config like `location ;` was valid for the parser and resulted in an empty
//   // token: std::string config_str_truly_empty = "location ;"; // This is not valid Nginx config
//   for
//   // a location If such a config *could* be parsed to an empty path token: NginxConfig
//   // temp_config_for_empty; NginxConfigStatement empty_path_stmt; empty_path_stmt.tokens_ =
//   // {"location", "", "StaticHandler"}; // Manually create scenario std::unique_ptr<NginxConfig>
//   // child = std::make_unique<NginxConfig>(); empty_path_stmt.child_block_ = std::move(child);
//   //
//   temp_config_for_empty.statements_.push_back(std::make_shared<NginxConfigStatement>(empty_path_stmt));
//   // EXPECT_FALSE(registry_.Init(temp_config_for_empty))
//   //     << "HandlerRegistry::Init should fail for a truly empty (unquoted) serving path.";
// }
