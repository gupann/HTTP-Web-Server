#include "static_handler.h"

namespace http = boost::beast::http;

void static_handler::handle_request(http::request<http::string_body>& req, http::response<http::string_body>& res) {
  
}

#endif
