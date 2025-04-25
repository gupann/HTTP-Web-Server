#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include "server.h"
#include "config_parser.h"
#include "logger.h"

int main(int argc, char* argv[]) {
    logger::init();
    BOOST_LOG_TRIVIAL(info) << "Server starting";

    if (argc != 2) {
        std::cerr << "Usage: webserver <config_file>\n";
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
    int port = GetPort(config);
    BOOST_LOG_TRIVIAL(info) << "Parsed config OK, using port " << port;

    // run server
    boost::asio::io_service io_service;
    server s(io_service, port);
    io_service.run();
    BOOST_LOG_TRIVIAL(info) << "Server shutting down";

    return 0;
}
