// Refactored server_main.cc code for the session class (manages reading/writing to a client socket)

#ifndef SESSION_H
#define SESSION_H

#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class session
{
public:
  session(boost::asio::io_service& io_service);

  tcp::socket& socket();

  void start();

private:
  void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
  void handle_write(const boost::system::error_code& error);

  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];

  std::string request_data_;
};

#endif