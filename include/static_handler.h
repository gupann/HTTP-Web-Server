#ifndef STATIC_HANDLER_H
#define STATIC_HANDLER_H

#include <boost/beast.hpp>
#include <memory>
#include <string>

#include "request_handler.h" // Request / Response aliases

namespace wasd::http {

class static_handler : public RequestHandler {
public:
  static_handler();
  static_handler(std::string prefix, std::string root_dir);

  std::unique_ptr<Response> handle_request(const Request &req) override;

private:
  // Helper utilities
  static std::string url_decode_simple(const std::string &encoded);
  static std::string get_mime_type(const std::string &ext_lower);

  // Per‑instance config captured from nginx stanza
  std::string prefix_;   // serving path (e.g. "/static")
  std::string root_dir_; // filesystem root (e.g. "./files")
};

} // namespace wasd::http

#endif // STATIC_HANDLER_H
