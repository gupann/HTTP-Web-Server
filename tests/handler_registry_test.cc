#include "gtest/gtest.h"
#include "handler_registry.h"
#include "config_parser.h"
#include "static_handler.h"
#include "echo_handler.h"

class HandlerRegistryTest : public ::testing::Test {
protected:
    HandlerRegistry registry_;
    NginxConfigParser parser_;
    NginxConfig config_;

    // Helper method to create a config from a string
    bool ParseConfig(const std::string& config_str) {
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
        location /echo EchoHandler;
    )";

    ASSERT_TRUE(ParseConfig(config_str));
    EXPECT_TRUE(registry_.Init(config_));

    // Check that the registry has the correct mappings
    request_handler* handler_root = registry_.Match("/");
    ASSERT_NE(handler_root, nullptr);
    EXPECT_TRUE(dynamic_cast<static_handler*>(handler_root) != nullptr);

    request_handler* handler_echo = registry_.Match("/echo");
    ASSERT_NE(handler_echo, nullptr);
    EXPECT_TRUE(dynamic_cast<echo_handler*>(handler_echo) != nullptr);
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
        location /api EchoHandler;
        location /api/v1 StaticHandler {
            root ./api_static;
        }
    )";

    ASSERT_TRUE(ParseConfig(config_str));
    EXPECT_TRUE(registry_.Init(config_));

    // Root handler for "/"
    request_handler* root_handler = registry_.Match("/");
    ASSERT_NE(root_handler, nullptr);
    EXPECT_TRUE(dynamic_cast<static_handler*>(root_handler) != nullptr);

    // API handler for "/api"
    request_handler* api_handler = registry_.Match("/api");
    ASSERT_NE(api_handler, nullptr);
    EXPECT_TRUE(dynamic_cast<echo_handler*>(api_handler) != nullptr);

    // Specific API v1 handler for "/api/v1"
    request_handler* api_v1_handler = registry_.Match("/api/v1/users");
    ASSERT_NE(api_v1_handler, nullptr);
    EXPECT_TRUE(dynamic_cast<static_handler*>(api_v1_handler) != nullptr);
}

// Test initialization with invalid handler type
TEST_F(HandlerRegistryTest, InvalidHandlerType) {
    std::string config_str = R"(
        location / UnknownHandler;
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
    request_handler* exact_handler = registry_.Match("/static");
    ASSERT_NE(exact_handler, nullptr);
    EXPECT_TRUE(dynamic_cast<static_handler*>(exact_handler) != nullptr);

    // Subpath match
    request_handler* subpath_handler = registry_.Match("/static/images");
    ASSERT_NE(subpath_handler, nullptr);
    EXPECT_TRUE(dynamic_cast<static_handler*>(subpath_handler) != nullptr);
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

    request_handler* root_handler = registry_.Match("/");
    ASSERT_NE(root_handler, nullptr);
    
    auto* static_handler_ptr = dynamic_cast<static_handler*>(root_handler);
    ASSERT_NE(static_handler_ptr, nullptr);
}

// Test no match returns nullptr
TEST_F(HandlerRegistryTest, NoMatchReturnsNullptr) {
    std::string config_str = R"(
        location /api EchoHandler;
    )";

    ASSERT_TRUE(ParseConfig(config_str));
    EXPECT_TRUE(registry_.Init(config_));

    EXPECT_EQ(registry_.Match("/nonexistent"), nullptr);
}
