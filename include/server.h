#ifndef SERVER_H
#define SERVER_H

#include <boost/asio.hpp>
#include "handler_registry.h"
#include "session.h"

using boost::asio::ip::tcp;

namespace wasd::http {

class server {
public:
  server(boost::asio::io_service &io_service, short port,
         std::shared_ptr<HandlerRegistry> registry);

  void handle_accept(session *new_session, const boost::system::error_code &error);

private:
  void start_accept();

  boost::asio::io_service &io_service_;
  tcp::acceptor acceptor_;
  std::shared_ptr<HandlerRegistry> registry_;
};

} // namespace wasd::http

#endif
