#include "gtest/gtest.h"
#include "config_parser.h"

class NginxConfigParserTestFixture : public testing::Test {
  protected:
    NginxConfigParserTestFixture() {

    }

    bool ParseConfig(const char* file_name) {
      return parser.Parse(file_name, &out_config);
    }

    NginxConfigParser parser;
    NginxConfig out_config;
};

TEST_F(NginxConfigParserTestFixture, SimpleConfig) {
  EXPECT_TRUE(ParseConfig("example_config"));
}

TEST_F(NginxConfigParserTestFixture, InvalidConfig) {
  EXPECT_FALSE(ParseConfig("invalid_config"));
}

TEST_F(NginxConfigParserTestFixture, EmptyConfig) {
  EXPECT_FALSE(ParseConfig("empty_config"));
}

TEST_F(NginxConfigParserTestFixture, StatementEmptyConfig) {
  EXPECT_FALSE(ParseConfig("statement_empty_config"));
}

TEST_F(NginxConfigParserTestFixture, NestedEmptyConfig) {
  EXPECT_FALSE(ParseConfig("nested_empty_config"));
}

TEST_F(NginxConfigParserTestFixture, BraceEmptyConfig) {
  EXPECT_TRUE(ParseConfig("brace_empty_config"));
}

TEST_F(NginxConfigParserTestFixture, NestedConfig) {
  EXPECT_TRUE(ParseConfig("nested_config"));
}

TEST_F(NginxConfigParserTestFixture, ExtraEndBraceConfig) {
  EXPECT_FALSE(ParseConfig("extra_end_brace_config"));
}