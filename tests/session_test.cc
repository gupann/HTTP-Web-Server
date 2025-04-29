#include "gtest/gtest.h"
#include "session.h"
#include "handler_registry.h"
#include <boost/asio.hpp>
#include <boost/beast/http.hpp>
#include <string>
#include <sstream>

namespace http = boost::beast::http;

class SessionTest : public ::testing::Test {
protected:
    boost::asio::io_service io_service_;

    std::shared_ptr<HandlerRegistry> registry_ =
    std::make_shared<HandlerRegistry>();

    session* test_session_ = new session{io_service_, registry_};

    void SimulateHandleRead(http::request<http::string_body> request,
                            const boost::system::error_code& ec = boost::system::error_code{},
                            size_t bytes_transferred = 100)
    {
        test_session_->set_request(request);
        test_session_->call_handle_read(ec, bytes_transferred);
    }

    // Helper function to serialize a request object to string
    std::string request_to_string(const http::request<http::string_body>& req) {
      std::ostringstream oss;
      oss << req;
      return oss.str();
    }
};

// Test case for handling a simple GET request
TEST_F(SessionTest, HandleSimpleGET) {
    // 1. Prepare a simple GET request
    http::request<http::string_body> req;
    req.method(http::verb::get);
    req.target("/");
    req.version(11); // HTTP/1.1
    req.set(http::field::host, "localhost");
    req.prepare_payload(); // Important for generating the string representation

    // Convert request to string for the expected echo body
    std::string expected_echo_body = request_to_string(req);

    // 2. Simulate handle_read being called after a successful read
    SimulateHandleRead(req);

    // 3. Check the generated response (res_) in the session
    auto res = test_session_->get_response();
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
    EXPECT_NO_THROW(test_session_->call_handle_read(ec, 0));
}

// Test case for handling a GET request with a specific path
TEST_F(SessionTest, HandleGETWithPath) {
  // 1. Prepare a GET request with a path
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/echo");
  req.version(11);
  req.set(http::field::host, "example.com");
  req.prepare_payload();

  std::string expected_echo_body = request_to_string(req);

  // 2. Simulate handle_read
  SimulateHandleRead(req);

  // 3. Check the response
  auto res = test_session_->get_response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.version(), 11);
  EXPECT_EQ(res[http::field::content_type], "text/plain");
  EXPECT_EQ(res.body(), expected_echo_body);
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
  req.prepare_payload(); // Calculates Content-Length

  std::string expected_echo_body = request_to_string(req);

  // 2. Simulate handle_read
  SimulateHandleRead(req);

  // 3. Check the response
  auto res = test_session_->get_response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.version(), 11);
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
  auto res = test_session_->get_response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.version(), 11);
  EXPECT_EQ(res[http::field::content_type], "text/plain");
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
  auto res = test_session_->get_response();
  EXPECT_EQ(res.result(), http::status::ok);
  EXPECT_EQ(res.version(), 11);
  EXPECT_EQ(res[http::field::content_type], "text/plain");
  EXPECT_EQ(res.body(), expected_echo_body);
}

// Expose private start() and handle_write() for testing
class TestSessionExposed : public session {
public:
  using session::start;
  using session::handle_write;
  TestSessionExposed(boost::asio::io_service& io, std::shared_ptr<HandlerRegistry> reg) : session(io, std::move(reg)) {}
};

// Cover session::start()
TEST_F(SessionTest, StartDoesNotThrow) {
  EXPECT_NO_THROW(test_session_->start());
}

// Cover the no-error, keep-alive branch of handle_write()
static bool write_deleted_flag = false;

TEST_F(SessionTest, HandleWrite_KeepAlive) {
  auto* s = new TestSessionExposed(io_service_, registry_);
  // HTTP/1.1 defaults to keep-alive
  boost::system::error_code ec; 
  EXPECT_NO_THROW(s->handle_write(ec, /*bytes_transferred=*/0));
  delete s;
}

// Cover both error and close branches of handle_write()
TEST_F(SessionTest, HandleWrite_ErrorDeletesSession) {
  write_deleted_flag = false;
  struct TempSessionExposed : TestSessionExposed {
    TempSessionExposed(boost::asio::io_service& io, std::shared_ptr<HandlerRegistry> reg)
      : TestSessionExposed(io, std::move(reg)) {}
    ~TempSessionExposed() override { write_deleted_flag = true; }
};
  auto* temp = new TempSessionExposed(io_service_, registry_);
  boost::system::error_code ec = boost::asio::error::operation_aborted;
  temp->handle_write(ec, /*bytes_transferred=*/0);
  EXPECT_TRUE(write_deleted_flag);
}

// Helper subclass to capture start() calls
struct ExposedSessionWithStart : TestSessionExposed {
  bool start_called = false;
  ExposedSessionWithStart(boost::asio::io_service& io, std::shared_ptr<HandlerRegistry> reg)
    : TestSessionExposed(io, std::move(reg)) {}
  void start() override { start_called = true; }
};
// Test that handle_write restarts on keep‐alive (HTTP/1.1)
TEST_F(SessionTest, HandleWrite_KeepAlive_CallsStart) {
  auto* s = new ExposedSessionWithStart(io_service_, registry_);
  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/");
  req.version(11);
  req.set(http::field::host, "localhost");
  req.prepare_payload();
  s->set_request(req);

  boost::system::error_code ec;
  s->handle_write(ec, /*bytes_transferred=*/10);
  EXPECT_TRUE(s->start_called);
  delete s;
}

// Test that handle_write deletes session when keep‐alive is false (HTTP/1.0)
TEST_F(SessionTest, HandleWrite_NoKeepAlive_DeletesSession) {
  bool deleted = false;
  struct TempSessionClose : TestSessionExposed {
    bool* flag;
    TempSessionClose(boost::asio::io_service& io, std::shared_ptr<HandlerRegistry> reg, bool* f)
      : TestSessionExposed(io, std::move(reg)), flag(f) {}
    ~TempSessionClose() override { *flag = true; }
  };
  auto* s = new TempSessionClose(io_service_, registry_, &deleted);

  http::request<http::string_body> req;
  req.method(http::verb::get);
  req.target("/");
  req.version(10);  // HTTP/1.0 → no keep‐alive
  req.set(http::field::host, "localhost");
  req.prepare_payload();
  s->set_request(req);

  boost::system::error_code ec;
  s->handle_write(ec, /*bytes_transferred=*/0);
  EXPECT_TRUE(deleted);
}

// Test that handle_write deletes session on error
TEST_F(SessionTest, HandleWrite_Error_DeletesSession) {
  bool deleted = false;
  struct TempSessionError : TestSessionExposed {
    bool* flag;
    TempSessionError(boost::asio::io_service& io, std::shared_ptr<HandlerRegistry> reg, bool* f)
      : TestSessionExposed(io, std::move(reg)), flag(f) {}
    ~TempSessionError() override { *flag = true; }
};
  auto* s = new TempSessionError(io_service_, registry_, &deleted);

  // any request is fine here
  s->set_request(http::request<http::string_body>{});
  boost::system::error_code ec = boost::asio::error::connection_aborted;
  s->handle_write(ec, /*bytes_transferred=*/0);
  EXPECT_TRUE(deleted);
}