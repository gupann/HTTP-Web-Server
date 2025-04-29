#include "request_handler.h"

request_handler::request_handler(const std::string& location, const std::string& root)
    : prefix_(location), dir_(root) {}

request_handler::~request_handler() = default;

std::string request_handler::get_prefix() const {
    return prefix_;
}

std::string request_handler::get_dir() const {
    return dir_;
}