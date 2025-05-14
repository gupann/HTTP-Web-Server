#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <sstream>
#include <string>
#include "config_parser.h"
#include "echo_handler.h"
#include "gtest/gtest.h"
#include "handler_registry.h"
#include "session.h"

using namespace wasd::http;
namespace http = boost::beast::http;

// helper class to expose protected hooks we need for testing
class TestableSession : public session {
public:
  using session::handle_read;
  using session::handle_write;
  using session::session; // inherit constructors
  using session::set_request;
  using session::start;
};

class SessionTest : public ::testing::Test {
protected:
  boost::asio::io_service io_service_;
  std::shared_ptr<HandlerRegistry> registry_ = std::make_shared<HandlerRegistry>();

  // use TestableSession so we can reach hooks without changing API
  TestableSession *test_session_ = new TestableSession{io_service_, registry_};
  NginxConfigParser parser_;
  NginxConfig config_;

  // Helper method to create a config from a string
  bool ParseConfig(const std::string &config_str) {
    std::stringstream ss(config_str);
    return parser_.Parse(&ss, &config_);
  }

  void SetUp() override {
    std::string config_str = R"(
    port 80;
    location /echo EchoHandler {};
    )";

    ParseConfig(config_str);
    registry_->Init(config_);
  }

  void SimulateHandleRead(http::request<http::string_body> request,
                          const boost::system::error_code &ec = {},
                          std::size_t bytes_transferred = 100) {
    test_session_->set_request(request);
    test_session_->handle_read(ec, bytes_transferred);
  }

  // Helper function to serialize a request object to string (for echo body comparison)
  std::string request_to_string(const http::request<http::string_body> &req) {
    std::ostringstream oss;
    oss << req;
    return oss.str();
  }
};

// Test case for handling a simple echo request
TEST_F(SessionTest, HandleEcho) {
  // 1. Prepare a simple GET request
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/echo");
  req.version(11); // HTTP/1.1
  req.set(http::field::host, "localhost");
  req.prepare_payload(); // Important for generating the string representation

  // Convert request to string for the expected echo body
  std::string expected_echo_body = request_to_string(req);

  // 2. Simulate handle_read being called after a successful read
  SimulateHandleRead(req);

  // 3. Check the generated response (res_) in the session
  const auto &res = test_session_->response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.version(), 11);
  EXPECT_EQ(res[http::field::content_type], "text/plain");
  EXPECT_EQ(res.body(), expected_echo_body);
}

// Test case for handle_read encountering an error
TEST_F(SessionTest, HandleReadError) {
  // 1. Prepare a dummy request (content doesn't matter much here)
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/");
  req.version(11);

  // 2. Simulate handle_read being called with an error
  // We expect the session to delete itself in this case, so we can't check state after.
  boost::system::error_code ec = boost::asio::error::connection_reset;
  EXPECT_NO_THROW(test_session_->handle_read(ec, 0));
}

// Test case for handling a POST request with a body
TEST_F(SessionTest, HandlePOSTRequest) {
  // 1. Prepare a POST request
  http::request<http::string_body> req;
  req.method(http::verb::post);
  req.target("/echo");
  req.version(11);
  req.set(http::field::host, "myserver");
  req.set(http::field::content_type, "application/x-www-form-urlencoded");
  req.body() = "name=test&value=123";
  req.prepare_payload(); // sets Content-Length

  std::string expected_echo_body = request_to_string(req);

  // 2. Simulate handle_read
  SimulateHandleRead(req);

  // 3. Check the response
  const auto &res = test_session_->response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res[http::field::content_type], "text/plain"); // Echo server responds with text/plain
  EXPECT_EQ(res.body(), expected_echo_body);
}

// Test case for handling a request with multiple headers
TEST_F(SessionTest, HandleRequestWithHeaders) {
  // 1. Prepare a request with multiple headers
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/echo");
  req.version(11);
  req.set(http::field::host, "monitor.com");
  req.set(http::field::user_agent, "TestClient/1.0");
  req.set(http::field::accept, "application/json");
  req.prepare_payload();

  std::string expected_echo_body = request_to_string(req);

  // 2. Simulate handle_read
  SimulateHandleRead(req);

  // 3. Check the response
  const auto &res = test_session_->response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), expected_echo_body);
}

// Test case for handling a request with "Connection: close"
TEST_F(SessionTest, HandleRequestConnectionClose) {
  // 1. Prepare a request asking to close the connection
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/echo");
  req.version(11);
  req.set(http::field::host, "secure.com");
  req.set(http::field::connection, "close"); // Request connection close
  req.prepare_payload();

  std::string expected_echo_body = request_to_string(req);

  // 2. Simulate handle_read
  SimulateHandleRead(req);

  // 3. Check the response
  const auto &res = test_session_->response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.body(), expected_echo_body);
}
// Cover session::start()
TEST_F(SessionTest, StartDoesNotThrow) {
  EXPECT_NO_THROW(test_session_->start());
}

// handle_write branches

// HTTP/1.1 keep‑alive branch
TEST_F(SessionTest, HandleWrite_KeepAlive) {
  auto *s = new TestableSession(io_service_, registry_);
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/");
  req.version(11); // keep‑alive by default
  req.set(http::field::host, "localhost");
  req.prepare_payload();
  s->set_request(req);

  EXPECT_NO_THROW(s->handle_write({}, 0));
  delete s;
}

// Cover both error and close branches of handle_write()
TEST_F(SessionTest, HandleWrite_ErrorDeletesSession_StaticFlag) {
  static bool write_deleted_flag = false;
  struct TempSession : TestableSession {
    using TestableSession::TestableSession;
    ~TempSession() override { write_deleted_flag = true; }
  };

  write_deleted_flag = false;
  auto *temp = new TempSession(io_service_, registry_);
  temp->handle_write(boost::asio::error::operation_aborted, 0);
  EXPECT_TRUE(write_deleted_flag);
}

// Branch verifies restart on keep‑alive
TEST_F(SessionTest, HandleWrite_KeepAlive_CallsStart) {
  struct Spy : TestableSession {
    using TestableSession::TestableSession;
    bool start_called = false;
    void start() override { start_called = true; }
  };

  auto *s = new Spy(io_service_, registry_);
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/");
  req.version(11);
  req.set(http::field::host, "localhost");
  req.prepare_payload();
  s->set_request(req);

  s->handle_write({}, 10);
  EXPECT_TRUE(s->start_called);
  delete s;
}

// Test that handle_write deletes session when keep‐alive is false (HTTP/1.0)
TEST_F(SessionTest, HandleWrite_NoKeepAlive_DeletesSession) {
  bool deleted = false;
  struct Tmp : TestableSession {
    bool *flag;
    Tmp(boost::asio::io_service &io, std::shared_ptr<HandlerRegistry> r, bool *f)
        : TestableSession(io, std::move(r)), flag(f) {}
    ~Tmp() override { *flag = true; }
  };

  auto *s = new Tmp(io_service_, registry_, &deleted);
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/");
  req.version(10); // HTTP/1.0 → no keep‐alive
  req.set(http::field::host, "localhost");
  req.prepare_payload();
  s->set_request(req);

  s->handle_write({}, 0);
  EXPECT_TRUE(deleted);
}

// Test that handle_write deletes session on error
TEST_F(SessionTest, HandleWrite_ErrorDeletesSession) {
  bool deleted = false;
  struct Tmp : TestableSession {
    bool *flag;
    Tmp(boost::asio::io_service &io, std::shared_ptr<HandlerRegistry> r, bool *f)
        : TestableSession(io, std::move(r)), flag(f) {}
    ~Tmp() override { *flag = true; }
  };

  auto *s = new Tmp(io_service_, registry_, &deleted);
  // any request is fine here
  s->set_request(http::request<http::string_body>{});
  s->handle_write(boost::asio::error::connection_aborted, 0);
  EXPECT_TRUE(deleted);
}