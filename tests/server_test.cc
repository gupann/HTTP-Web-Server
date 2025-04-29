#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "server.h"
#include "session.h"
#include "handler_registry.h"
#include <boost/asio.hpp>

using ::testing::StrictMock;

// 1) a mockable session
struct MockSession : session {
  MockSession(boost::asio::io_service& io,
              std::shared_ptr<HandlerRegistry> reg)
      : session(io, std::move(reg)) {}
  MOCK_METHOD(void, start, (), (override));
};

// file‐static flag
static bool session_deleted_flag = false;

// TempSession at namespace scope
struct TempSession : MockSession {
  TempSession(boost::asio::io_service& io,
              std::shared_ptr<HandlerRegistry> reg)
      : MockSession(io, std::move(reg)) {}
  ~TempSession() override { session_deleted_flag = true; }
};

// 2) test fixture
class ServerTest : public ::testing::Test {
protected:
  boost::asio::io_service io_;
  std::shared_ptr<HandlerRegistry> registry_ =
      std::make_shared<HandlerRegistry>();

  StrictMock<MockSession>* mock_sess_;
  std::unique_ptr<server> srv_;

  void SetUp() override {
    mock_sess_ = new StrictMock<MockSession>(io_, registry_);
    srv_ = std::make_unique<server>(io_, /*port=*/5555, registry_);
  }

  void TearDown() override {
    delete mock_sess_;
  }
};

// 3) success path: handle_accept should call start()
TEST_F(ServerTest, HandleAcceptSuccess_CallsStart) {
  boost::system::error_code no_error;
  EXPECT_CALL(*mock_sess_, start()).Times(1);
  srv_->handle_accept(mock_sess_, no_error);
}

// 4) error path: handle_accept should delete session
TEST_F(ServerTest, HandleAcceptError_DeletesSession) {
  session_deleted_flag = false;
  auto* temp = new TempSession(io_, registry_);
  boost::system::error_code ec = boost::asio::error::operation_aborted;
  srv_->handle_accept(temp, ec);
  EXPECT_TRUE(session_deleted_flag) << "session was not deleted on error";
}

// 5) calling handle_accept() multiple times still calls start() every time
TEST_F(ServerTest, HandleAccept_CalledMultipleTimes) {
  boost::system::error_code no_error;

  // second mock for the second call
  StrictMock<MockSession> second_mock(io_, registry_);

  // expect start() on both mocks
  EXPECT_CALL(*mock_sess_, start()).Times(1);
  EXPECT_CALL(second_mock, start()).Times(1);

  // call both accepts
  srv_->handle_accept(mock_sess_, no_error);
  srv_->handle_accept(&second_mock, no_error);
}
