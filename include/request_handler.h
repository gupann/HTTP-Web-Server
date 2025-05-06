#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <boost/beast.hpp>

namespace wasd::http {

namespace http = boost::beast::http;

class request_handler {
public:
  virtual ~request_handler();
  request_handler(const std::string &location, const std::string &root);

  virtual void handle_request(http::request<http::string_body> &req,
                              http::response<http::string_body> &res) = 0;

  std::string get_prefix() const;
  std::string get_dir() const;

private:
  std::string prefix_;
  std::string dir_;
};

} // namespace wasd::http

#endif
