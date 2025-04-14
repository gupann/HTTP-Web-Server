// Refactored server_main.cc code for the session class (manages reading/writing to a client socket)

#ifndef SESSION_H
#define SESSION_H

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>

using boost::asio::ip::tcp;

class session
{
public:
  session(boost::asio::io_service& io_service);

  tcp::socket& socket();

  void start();

private:
  void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
  void handle_write(const boost::system::error_code& error, size_t bytes_transferred);

  tcp::socket socket_;

  boost::beast::flat_buffer buffer_;
  boost::beast::http::request<boost::beast::http::string_body> req_;
  boost::beast::http::response<boost::beast::http::string_body> res_;
};

#endif