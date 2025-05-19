#include "crud_handler.h"
#include "echo_handler.h"
#include "gtest/gtest.h"
#include "not_found_handler.h"
#include "request_handler.h"
#include "static_handler.h"

#include <boost/beast/http.hpp>
#include <filesystem>
#include <fstream>
#include <sstream> // Required for std::ostringstream

using namespace wasd::http;

namespace http = boost::beast::http;

// Global utility function to create HTTP requests for tests
http::request<http::string_body> create_request(http::verb method, const std::string &target,
                                                const std::string &body = "") {
  http::request<http::string_body> req;
  req.method(method);
  req.target(target);
  req.version(11); // HTTP/1.1
  req.set(http::field::host, "localhost");
  if (!body.empty()) {
    req.body() = body;
  }
  req.prepare_payload(); // Sets content-length, etc. if body is present
  return req;
}

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
};

// ==========================================================================
// Test Cases
// ==========================================================================

// Test serving a simple text file
TEST_F(StaticHandlerIntegrationTest, ServeExistingTxtFile) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/static/hello.txt");

  auto res = static_handler_->handle_request(req);
  // Assert
  EXPECT_EQ(res->result(), http::status::ok);
  ASSERT_TRUE(res->has_content_length());
  EXPECT_EQ((*res)[http::field::content_type], "text/plain");
  EXPECT_EQ(res->body(), "Hello Text!");
}

// Test serving a simple HTML file
TEST_F(StaticHandlerIntegrationTest, ServeExistingHtmlFile) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/static/page.html");

  auto res = static_handler_->handle_request(req);
  // Assert
  EXPECT_EQ(res->result(), http::status::ok);
  EXPECT_EQ((*res)[http::field::content_type], "text/html");
  EXPECT_EQ(res->body(), "<html></html>");
}

// Test requesting a file that doesn't exist
TEST_F(StaticHandlerIntegrationTest, FileNotFound) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/static/nonexistent.jpg");

  auto res = static_handler_->handle_request(req);

  // Assert
  EXPECT_EQ(res->result(), http::status::not_found);
}

// Test basic path traversal prevention (using '..')
TEST_F(StaticHandlerIntegrationTest, PathTraversalBlockedSimple) {
  // Arrange: Attempt to access something outside the root via '..'
  http::request<http::string_body> req =
      create_request(http::verb::get, "/static/../some_other_file");

  auto res = static_handler_->handle_request(req);

  // Assert: Expect 404 Not Found (or potentially 403 Forbidden)
  // because the path sanitization should reject '..' components.
  EXPECT_EQ(res->result(), http::status::not_found);
}

// Test URL decoding for filenames with spaces
TEST_F(StaticHandlerIntegrationTest, ServeFileWithEncodedSpaces) {
  // Arrange: Request uses %20 for spaces
  http::request<http::string_body> req =
      create_request(http::verb::get, "/static/file%20with%20spaces.txt");

  auto res = static_handler_->handle_request(req);

  // Assert
  EXPECT_EQ(res->result(), http::status::ok);
  EXPECT_EQ((*res)[http::field::content_type], "text/plain");
  EXPECT_EQ(res->body(), "Content of file with spaces");
}

// Test requesting the root of the static path (behavior depends on handler implementation)
// Often this might serve an index.html file if present, or return 404/403 if directory listing is
// disabled. Assuming here it should fail if index.html doesn't exist.
TEST_F(StaticHandlerIntegrationTest, RequestStaticRootWithoutIndex) {
  // Arrange
  http::request<http::string_body> req =
      create_request(http::verb::get, "/static/"); // Request the root

  auto res = static_handler_->handle_request(req);

  // Assert: Expect 404 Not Found if no index.html exists and directory listing is off.
  // Adjust assertion based on your handler's intended behavior for directory requests.
  EXPECT_EQ(res->result(), http::status::not_found);
}

// ==========================================================================
// Test Fixture: EchoHandlerTest
// ==========================================================================
class EchoHandlerTest : public ::testing::Test {
protected:
  std::unique_ptr<echo_handler> handler_;

  void SetUp() override {
    // echo_handler now takes a prefix string or has a default constructor.
    // Using the prefix constructor as per echo_handler.cc.
    handler_ = std::make_unique<echo_handler>("/echo");
  }
};

// Test Cases for EchoHandler
TEST_F(EchoHandlerTest, EchoesRequest) {
  // Arrange
  http::request<http::string_body> req =
      create_request(http::verb::post, "/echo/test", "Test Body Content");
  req.set(http::field::content_type, "text/plain"); // Add a header for more complete testing
  req.set("X-Custom-Header", "CustomValue");        // Add another header

  // The echo_handler serializes the entire request (method, target, version, headers, body)
  // into the response body.
  std::ostringstream oss_req;
  oss_req << req;
  std::string expected_body = oss_req.str();

  // Act
  std::unique_ptr<Response> res = handler_->handle_request(req);

  // Assert
  ASSERT_NE(res, nullptr);
  EXPECT_EQ(res->result(), http::status::ok);
  ASSERT_TRUE(res->has_content_length());
  EXPECT_EQ((*res)[http::field::content_type], "text/plain");
  EXPECT_EQ(res->body(), expected_body);
}

// ==========================================================================
// Test Fixture: NotFoundHandlerTest
// ==========================================================================
class NotFoundHandlerTest : public ::testing::Test {
protected:
  std::unique_ptr<not_found_handler> handler_;

  void SetUp() override {
    // not_found_handler now has a default constructor.
    handler_ = std::make_unique<not_found_handler>();
  }
};

// Test Cases for NotFoundHandler
TEST_F(NotFoundHandlerTest, Returns404) {
  // Arrange
  http::request<http::string_body> req = create_request(http::verb::get, "/some/nonexistent/path");

  // Act
  std::unique_ptr<Response> res = handler_->handle_request(req);

  // Assert
  ASSERT_NE(res, nullptr);
  EXPECT_EQ(res->result(), http::status::not_found);
  ASSERT_TRUE(res->has_content_length());
  EXPECT_EQ((*res)[http::field::content_type], "text/plain");
  EXPECT_EQ(res->body(), "404 Not Found");
}

// Mock filesystem for testing
class MockFileSystem : public FileSystemInterface {
public:
  // Mock storage for our "files"
  std::unordered_map<std::string, std::string> mock_files;
  std::unordered_map<std::string, std::vector<std::string>> mock_directories;

  bool file_exists(const std::string &path) const override {
    return mock_files.find(path) != mock_files.end() ||
           mock_directories.find(path) != mock_directories.end();
  }

  std::optional<std::string> read_file(const std::string &path) const override {
    auto it = mock_files.find(path);
    if (it != mock_files.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool write_file(const std::string &path, const std::string &content) override {
    mock_files[path] = content;

    // Extract directory from path
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
      std::string dir = path.substr(0, last_slash);
      std::string filename = path.substr(last_slash + 1);

      // Ensure directory exists in our mock
      if (mock_directories.find(dir) == mock_directories.end()) {
        mock_directories[dir] = std::vector<std::string>();
      }

      // Add filename to directory if not already there
      auto &files = mock_directories[dir];
      if (std::find(files.begin(), files.end(), filename) == files.end()) {
        files.push_back(filename);
      }
    }

    return true;
  }

  bool delete_file(const std::string &path) override {
    auto it = mock_files.find(path);
    if (it != mock_files.end()) {
      mock_files.erase(it);

      // Remove from directory listing too
      size_t last_slash = path.find_last_of('/');
      if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash);
        std::string filename = path.substr(last_slash + 1);

        auto dir_it = mock_directories.find(dir);
        if (dir_it != mock_directories.end()) {
          auto &files = dir_it->second;
          files.erase(std::remove(files.begin(), files.end(), filename), files.end());
        }
      }

      return true;
    }
    return false;
  }

  bool create_directory(const std::string &path) override {
    mock_directories[path] = std::vector<std::string>();
    return true;
  }

  std::vector<std::string> list_directory(const std::string &path) const override {
    auto it = mock_directories.find(path);
    if (it != mock_directories.end()) {
      return it->second;
    }
    return std::vector<std::string>();
  }
};

// ==========================================================================
// Test Fixture: CrudHandlerTest
// ==========================================================================
class CrudHandlerTest : public ::testing::Test {
protected:
  void SetUp() override {
    mock_fs = std::make_shared<MockFileSystem>();
    handler = std::make_unique<CrudRequestHandler>("/api", "/data", mock_fs);
  }

  std::shared_ptr<MockFileSystem> mock_fs;
  std::unique_ptr<CrudRequestHandler> handler;
};

// Test getting an entity that exists
TEST_F(CrudHandlerTest, GetExistingEntity) {
  // Setup mock file
  mock_fs->write_file("/data/Shoes/1", "{\"name\":\"Nike\",\"size\":10}");

  // Create request
  auto req = create_request(http::verb::get, "/api/Shoes/1");

  // Process request
  auto res = handler->handle_request(req);

  // Verify
  EXPECT_EQ(res->result(), http::status::ok);
  EXPECT_EQ(res->body(), "{\"name\":\"Nike\",\"size\":10}");
}

// Test getting an entity that doesn't exist
TEST_F(CrudHandlerTest, GetNonExistentEntity) {
  // Create request
  auto req = create_request(http::verb::get, "/api/Shoes/999");

  // Process request
  auto res = handler->handle_request(req);

  // Verify
  EXPECT_EQ(res->result(), http::status::not_found);
  EXPECT_TRUE(res->body().find("not found") != std::string::npos);
}

// Test listing entities when some exist
TEST_F(CrudHandlerTest, ListExistingEntities) {
  // Setup mock files
  mock_fs->write_file("/data/Shoes/1", "{\"name\":\"Nike\",\"size\":10}");
  mock_fs->write_file("/data/Shoes/2", "{\"name\":\"Adidas\",\"size\":9}");

  // Create request
  auto req = create_request(http::verb::get, "/api/Shoes");

  // Process request
  auto res = handler->handle_request(req);

  // Verify
  EXPECT_EQ(res->result(), http::status::ok);
  // Check that the response contains both IDs
  EXPECT_TRUE(res->body().find("\"1\"") != std::string::npos);
  EXPECT_TRUE(res->body().find("\"2\"") != std::string::npos);
}

// Test listing entities when none exist
TEST_F(CrudHandlerTest, ListEmptyEntities) {
  // Create request
  auto req = create_request(http::verb::get, "/api/EmptyCategory");

  // Process request
  auto res = handler->handle_request(req);

  // Verify
  EXPECT_EQ(res->result(), http::status::ok);
  EXPECT_EQ(res->body(), "[]"); // Empty JSON array
}

// Test path parsing
TEST_F(CrudHandlerTest, PathParsing) {
  // Test parsing paths with entity and ID
  {
    auto [entity, id] = handler->parse_path("/Shoes/1");
    EXPECT_EQ(entity, "Shoes");
    EXPECT_TRUE(id.has_value());
    EXPECT_EQ(id.value(), "1");
  }

  // Test parsing paths with just entity
  {
    auto [entity, id] = handler->parse_path("/Shoes");
    EXPECT_EQ(entity, "Shoes");
    EXPECT_FALSE(id.has_value());
  }

  // Test without leading slash
  {
    auto [entity, id] = handler->parse_path("Shoes");
    EXPECT_EQ(entity, "Shoes");
    EXPECT_FALSE(id.has_value());
  }

  // Test invalid paths
  {
    auto [entity, id] = handler->parse_path("/");
    EXPECT_EQ(entity, "");
    EXPECT_FALSE(id.has_value());
  }

  // Test path with too many segments
  {
    auto [entity, id] = handler->parse_path("/Shoes/1/extra");
    EXPECT_EQ(entity, "");
    EXPECT_FALSE(id.has_value());
  }
}