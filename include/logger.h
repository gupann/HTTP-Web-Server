#ifndef LOGGER_H
#define LOGGER_H

#include <boost/log/trivial.hpp>
#include <string>

namespace logger {

void init(const std::string &file_pattern = "logs/server_%Y-%m-%d_%N.log");

}

#endif