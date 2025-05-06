#include "gtest/gtest.h"
#include "request_handler.h"
#include "static_handler.h"

#include <boost/beast/http.hpp>
#include <filesystem>
#include <fstream>

using namespace wasd::http;

namespace http = boost::beast::http;

// ==========================================================================
// Test Fixture: StaticHandlerIntegrationTest
// ==========================================================================
class StaticHandlerIntegrationTest : public ::testing::Test {
protected:
  std::filesystem::path temp_dir_;
  std::shared_ptr<static_handler> static_handler_;

  void SetUp() override {
    std::string temp_dir_name = "static_handler_test_" + std::to_string(getpid());
    temp_dir_ = std::filesystem::temp_directory_path() / temp_dir_name;

    std::error_code ec;
    std::filesystem::create_directories(temp_dir_, ec);
    ASSERT_FALSE(ec) << "Failed to create temporary directory: " << temp_dir_.string()
                     << " Error: " << ec.message();

    try {
      static_handler_ = std::make_shared<static_handler>("/static", temp_dir_.string());
    } catch (const std::exception &e) {
      FAIL() << "Failed to instantiate static_handler: " << e.what();
    }
    ASSERT_TRUE(static_handler_ != nullptr)
        << "static_handler_ pointer is null after instantiation.";

    CreateTestFile("hello.txt", "Hello Text!");
    CreateTestFile("page.html", "<html></html>");
    CreateTestFile("file with spaces.txt", "Content of file with spaces");
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(temp_dir_, ec);
    if (ec) {
      std::cerr << "Warning: Failed to remove temp dir: " << temp_dir_.string()
                << " Error: " << ec.message() << std::endl;
    }
  }

  void CreateTestFile(const std::string &filename, const std::string &content) {
    std::ofstream ofs(temp_dir_ / filename);
    ASSERT_TRUE(ofs.is_open()) << "Failed to open file for writing: "
                               << (temp_dir_ / filename).string();
    ofs << content;
    ASSERT_TRUE(ofs.good()) << "Failed write to file: " << (temp_dir_ / filename).string();
    ofs.close();
  }

  http::request<http::string_body> create_request(http::verb method, const std::string &target) {
    http::request<http::string_body> req;
    req.method(method);
    req.target(target);
    req.version(11);
    req.set(http::field::host, "localhost");
    req.prepare_payload();
    return req;
  }
};

// ==========================================================================
// Test Cases
// ==========================================================================

// Test serving a simple text file
TEST_F(StaticHandlerIntegrationTest, ServeExistingTxtFile) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/static/hello.txt");
  http::response<http::string_body> res;

  // Act
  static_handler_->handle_request(req, res);

  // Assert
  EXPECT_EQ(res.result(), http::status::ok);
  ASSERT_TRUE(res.has_content_length());
  EXPECT_EQ(res[http::field::content_type], "text/plain");
  EXPECT_EQ(res.body(), "Hello Text!");
}

// Test serving a simple HTML file
TEST_F(StaticHandlerIntegrationTest, ServeExistingHtmlFile) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/static/page.html");
  http::response<http::string_body> res;

  // Act
  static_handler_->handle_request(req, res);

  // Assert
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res[http::field::content_type], "text/html");
  EXPECT_EQ(res.body(), "<html></html>");
}

// Test requesting a file that doesn't exist
TEST_F(StaticHandlerIntegrationTest, FileNotFound) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/static/nonexistent.jpg");
  http::response<http::string_body> res;

  // Act
  static_handler_->handle_request(req, res);

  // Assert
  EXPECT_EQ(res.result(), http::status::not_found);
}

// Test basic path traversal prevention (using '..')
TEST_F(StaticHandlerIntegrationTest, PathTraversalBlockedSimple) {
  // Arrange: Attempt to access something outside the root via '..'
  http::request<http::string_body> req =
      create_request(http::verb::get, "/static/../some_other_file");
  http::response<http::string_body> res;

  // Act
  static_handler_->handle_request(req, res);

  // Assert: Expect 404 Not Found (or potentially 403 Forbidden)
  // because the path sanitization should reject '..' components.
  EXPECT_EQ(res.result(), http::status::not_found);
}

// Test URL decoding for filenames with spaces
TEST_F(StaticHandlerIntegrationTest, ServeFileWithEncodedSpaces) {
  // Arrange: Request uses %20 for spaces
  http::request<http::string_body> req =
      create_request(http::verb::get, "/static/file%20with%20spaces.txt");
  http::response<http::string_body> res;

  // Act
  static_handler_->handle_request(req, res);

  // Assert
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res[http::field::content_type], "text/plain");
  EXPECT_EQ(res.body(), "Content of file with spaces");
}

// Test requesting the root of the static path (behavior depends on handler implementation)
// Often this might serve an index.html file if present, or return 404/403 if directory listing is
// disabled. Assuming here it should fail if index.html doesn't exist.
TEST_F(StaticHandlerIntegrationTest, RequestStaticRootWithoutIndex) {
  // Arrange
  http::request<http::string_body> req =
      create_request(http::verb::get, "/static/"); // Request the root
  http::response<http::string_body> res;

  // Act
  static_handler_->handle_request(req, res);

  // Assert: Expect 404 Not Found if no index.html exists and directory listing is off.
  // Adjust assertion based on your handler's intended behavior for directory requests.
  EXPECT_EQ(res.result(), http::status::not_found);
}