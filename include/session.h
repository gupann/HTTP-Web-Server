#ifndef SESSION_H
#define SESSION_H

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <unordered_map>
#include "handler_registry.h"
#include "request_handler.h"

namespace http = boost::beast::http;
using boost::asio::ip::tcp;

class session : public std::enable_shared_from_this<session> {
public:
  session(boost::asio::io_service &io_service, std::shared_ptr<HandlerRegistry> registry);

  virtual ~session();

  tcp::socket &socket();

  virtual void start();

  http::response<http::string_body> get_response();

  void set_request(http::request<http::string_body> req);
  void call_handle_read(const boost::system::error_code &error, size_t bytes_transferred);
  void handle_write(const boost::system::error_code &error, size_t bytes_transferred);

private:
  void handle_read(const boost::system::error_code &error, size_t bytes_transferred);

  tcp::socket socket_;

  boost::beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  http::response<http::string_body> res_;

  std::chrono::steady_clock::time_point start_time_;

  std::unordered_map<std::string, std::shared_ptr<request_handler>> routes;

  std::shared_ptr<HandlerRegistry> registry_;
};

#endif