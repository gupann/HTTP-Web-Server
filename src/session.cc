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
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        boost::bind(&session::handle_read, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
}

void session::handle_read(const boost::system::error_code& error, 
              size_t bytes_transferred)
{
    if (!error)
    {      
      request_data_.append(data_, bytes_transferred);
      if (request_data_.find("\n\n") != std::string::npos) {
        boost::asio::async_write(socket_,
          boost::asio::buffer(request_data_, request_data_.size()),
          boost::bind(&session::handle_write, this,
            boost::asio::placeholders::error));
        request_data_.clear();
      }
      else {
        start();
      }
    }
    else
    {
      delete this;
    }
}

void session::handle_write(const boost::system::error_code& error)
{
    if (!error)
    {
      socket_.async_read_some(boost::asio::buffer(data_, max_length),
          boost::bind(&session::handle_read, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }
    else
    {
      delete this;
    }
}