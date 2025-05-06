#include "server.h"

using namespace wasd::http;

server::server(boost::asio::io_service &io_service, short port,
               std::shared_ptr<HandlerRegistry> registry)
    : io_service_(io_service), acceptor_(io_service, tcp::endpoint(tcp::v4(), port)),
      registry_(std::move(registry)) {
  start_accept();
}

void server::start_accept() {
  session *new_session = new session(io_service_, registry_);
  acceptor_.async_accept(new_session->socket(),
                         [this, new_session](const boost::system::error_code &error) {
                           this->handle_accept(new_session, error);
                         });
}

void server::handle_accept(session *new_session, const boost::system::error_code &error) {
  if (!error) {
    new_session->start();
  } else {
    delete new_session;
  }

  start_accept();
}