#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <boost/beast.hpp>

namespace http = boost::beast::http;

class request_handler
{
public:
  request_handler(const std::string& location, const std::string& root)
  : dir_(root), path_(location) {}
  
  virtual void handle_request(
    http::request<http::string_body>& req,
    http::response<http::string_body>& res) = 0;

private:
  std::string dir_;
  std::string path_;
};

#endif