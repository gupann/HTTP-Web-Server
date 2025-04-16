// Refactored server_main.cc code for the session class (manages reading/writing to a client socket)

#include "session.h"
#include <boost/bind.hpp>
#include <iostream> // for server debugging output

session::session(boost::asio::io_service& io_service)
    : socket_(io_service)
{
}

tcp::socket& session::socket()
{
  return socket_;
}

void session::start()
{
  boost::beast::http::async_read(socket_, buffer_, req_,
    boost::bind(&session::handle_read, this,
      boost::asio::placeholders::error,
      boost::asio::placeholders::bytes_transferred));
}

void session::call_handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
  handle_read(error, bytes_transferred);
}

boost::beast::http::response<boost::beast::http::string_body> session::get_response() {
  return res_;
}

boost::beast::http::request<boost::beast::http::string_body> session::get_request() {
  return req_;
}

void session::set_request(boost::beast::http::request<boost::beast::http::string_body> req) {
  req_ = req;
}

void session::handle_read(const boost::system::error_code& error, 
              size_t bytes_transferred)
{
    if (!error)
    {
      std::cout << "Valid HTTP Request received. (" << bytes_transferred << " bytes):\n";
      std::cout << req_ << std::endl;

      std::ostringstream oss;
      oss << req_;
      std::string echo = oss.str(); // GET does not have body. Creating the echo string

      res_.version(req_.version());
      res_.result(boost::beast::http::status::ok);
      res_.set(boost::beast::http::field::content_type, "text/plain");
      res_.body() = echo;
      // res_.prepare_payload(); // for auto encoding and byte size etc.

      boost::beast::http::async_write(socket_, res_,
        boost::bind(&session::handle_write, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      std::cerr << "Error in handle_read: " << error.message() << std::endl;
      delete this;
    }
}

void session::handle_write(const boost::system::error_code& error, size_t bytes_transferred)
{
    if (!error)
    {
      std::cout << "Response sent (" << bytes_transferred << " bytes)\n\n\n";
      if (req_.keep_alive()) {
        start();
      }
      else {
        std::cout << "Close connection requested by client" << std::endl;
        delete this;
      }
    }
    else
    {
      std::cerr << "Error in handle_write: " << error.message() << std::endl;
      delete this;
    }
}