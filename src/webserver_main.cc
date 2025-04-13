#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include "server.h"
#include "config_parser.h"

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: webserver <config_file>\n";
        return 1;
    }

    // parse the config file
    NginxConfigParser parser;
    NginxConfig config;
    if (!parser.Parse(argv[1], &config)) {
      std::cerr << "Failed to parse config file!\n";
      return 1;
    }

    // get the port from config
    int port = GetPort(config);
    std::cout << "Using port: " << port << std::endl;

    // run server
    boost::asio::io_service io_service;
    server s(io_service, port);
    io_service.run();
    return 0;
}
