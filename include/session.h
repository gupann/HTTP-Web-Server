#ifndef SESSION_H
#define SESSION_H

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <memory>
#include <unordered_map>
#include "handler_registry.h"
#include "request_handler.h"

namespace wasd::http {

namespace http = boost::beast::http;
using boost::asio::ip::tcp;

class session : public std::enable_shared_from_this<session> {
public:
  session(boost::asio::io_service &io_service, std::shared_ptr<HandlerRegistry> registry);
  virtual ~session();

  tcp::socket &socket();
  virtual void start();

  const http::response<http::string_body> &response() const { return res_; }

protected:
  // visible to test subclasses via `using`
  void set_request(const http::request<http::string_body> &req) { req_ = req; }
  void handle_read(const boost::system::error_code &err, std::size_t bytes);
  void handle_write(const boost::system::error_code &err, std::size_t bytes);

private:
  tcp::socket socket_;

  boost::beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  http::response<http::string_body> res_;

  std::chrono::steady_clock::time_point start_time_;

  std::shared_ptr<HandlerRegistry> registry_;
};

} // namespace wasd::http

#endif
