#ifndef SESSION_H
#define SESSION_H

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
using boost::asio::ip::tcp;

class session {
public:
  session(boost::asio::io_service& io_service);
  virtual ~session(); // make dtor virtual

  tcp::socket& socket();

  virtual void start();

  boost::beast::http::response<boost::beast::http::string_body> get_response();

  void set_request(boost::beast::http::request<boost::beast::http::string_body> req);
  void call_handle_read(const boost::system::error_code& error, size_t bytes_transferred);
  void handle_write(const boost::system::error_code& error, size_t bytes_transferred);

private:
  void handle_read(const boost::system::error_code& error, size_t bytes_transferred);

  tcp::socket socket_;

  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> res_;
};

#endif