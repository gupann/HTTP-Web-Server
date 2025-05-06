#include <boost/asio.hpp>
#include <boost/asio/signal_set.hpp>
#include <cstdlib>
#include <iostream>
#include "config_parser.h"
#include "logger.h"
#include "server.h"

int main(int argc, char *argv[]) {
  logger::init();
  BOOST_LOG_TRIVIAL(info) << "Server starting";

  if (argc != 2) {
    BOOST_LOG_TRIVIAL(error) << "Usage: webserver <config_file>";
    return 1;
  }

  // parse the config file
  NginxConfigParser parser;
  NginxConfig config;
  if (!parser.Parse(argv[1], &config)) {
    BOOST_LOG_TRIVIAL(error) << "Failed to parse config file!";
    return 1;
  }

  // get the port from config
  int port = 0;
  try {
    port = GetPort(config);
  } catch (const std::exception &e) {
    BOOST_LOG_TRIVIAL(error) << "Invalid port value in config: " << e.what();
    return 1;
  }
  if (port <= 0 || port > 65535) {
    BOOST_LOG_TRIVIAL(error) << "Port out of range (1–65535): " << port;
    return 1;
  }
  BOOST_LOG_TRIVIAL(info) << "Parsed config OK, using port " << port;

  auto registry = std::make_shared<HandlerRegistry>();
  if (!registry->Init(config)) {
    BOOST_LOG_TRIVIAL(error) << "Failed to build handler registry";
    return 1;
  }

  // set up ASIO & signal handler
  boost::asio::io_service io_service;
  boost::asio::signal_set shutdown_signals(io_service, SIGINT, SIGTERM);
  shutdown_signals.async_wait([&](const boost::system::error_code &, int) {
    BOOST_LOG_TRIVIAL(info) << "Server shutting down";
    io_service.stop();
  });

  // start server
  server s(io_service, static_cast<short>(port), registry);
  io_service.run();

  return 0;
}
