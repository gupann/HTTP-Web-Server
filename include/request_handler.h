#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <boost/beast.hpp>
#include <memory> // <- for std::unique_ptr

namespace wasd::http {

namespace http = boost::beast::http;
using Request = http::request<http::string_body>;
using Response = http::response<http::string_body>;

class RequestHandler {
public:
  virtual ~RequestHandler() = default;

  // Concrete handlers will implement exactly one ctor with typed args,
  // then override this method to generate the response.
  virtual std::unique_ptr<Response> handle_request(const Request &req) = 0;
};

} // namespace wasd::http

#endif // REQUEST_HANDLER_H
