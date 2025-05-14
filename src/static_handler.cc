#include "static_handler.h"

#include <algorithm>
#include <boost/log/trivial.hpp>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include "handler_factory.h"

using namespace wasd::http;
namespace http = boost::beast::http;

static_handler::static_handler() : prefix_("/"), root_dir_(".") {}
static_handler::static_handler(std::string prefix, std::string root)
    : prefix_(std::move(prefix)), root_dir_(std::move(root)) {}

// url‑decode helper
std::string static_handler::url_decode_simple(const std::string &enc) {
  std::string dec;
  dec.reserve(enc.size());
  for (size_t i = 0; i < enc.size(); ++i) {
    if (enc[i] == '%' && i + 2 < enc.size() && enc[i + 1] == '2' && enc[i + 2] == '0') {
      dec += ' ';
      i += 2;
    } else if (enc[i] == '+') {
      dec += ' ';
    } else {
      dec += enc[i];
    }
  }
  return dec;
}

// mime helper
std::string static_handler::get_mime_type(const std::string &ext) {
  static const std::unordered_map<std::string, std::string> map = {
      {".html", "text/html"},      {".htm", "text/html"},
      {".css", "text/css"},        {".js", "application/javascript"},
      {".jpeg", "image/jpeg"},     {".jpg", "image/jpeg"},
      {".png", "image/png"},       {".gif", "image/gif"},
      {".txt", "text/plain"},      {".zip", "application/zip"},
      {".pdf", "application/pdf"}, {".ico", "image/x-icon"},
      {".svg", "image/svg+xml"}};

  auto it = map.find(ext);
  return it != map.end() ? it->second : "application/octet-stream";
}

// handle_request
std::unique_ptr<Response> static_handler::handle_request(const Request &req) {
  auto res = std::make_unique<Response>();

  // 1. prefix check & path construction
  std::string decoded = url_decode_simple(std::string(req.target()));
  if (decoded.rfind(prefix_, 0) != 0) { // routing error
    BOOST_LOG_TRIVIAL(error) << "StaticHandler: prefix mismatch: " << decoded;
    res->result(http::status::not_found);
    res->set(http::field::content_type, "text/plain");
    res->body() = "404 Not Found";
    res->prepare_payload();
    return res;
  }

  std::string rel = decoded.substr(prefix_.size());
  if (rel.empty() || rel[0] != '/')
    rel = '/' + rel;

  std::string path = root_dir_;
  if (path.size() > 1 && path.back() == '/')
    path.pop_back();
  path += rel;

  // basic “…/..” traversal protection
  if (path.find("..") != std::string::npos || path.compare(0, root_dir_.size(), root_dir_) != 0) {
    BOOST_LOG_TRIVIAL(warning) << "Directory traversal attempt: " << path;
    res->result(http::status::not_found);
    res->set(http::field::content_type, "text/plain");
    res->body() = "404 Not Found";
    res->prepare_payload();
    return res;
  }

  // 2. open & read file
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec)) {
    BOOST_LOG_TRIVIAL(warning) << "File not found: " << path;
    res->result(http::status::not_found);
    res->set(http::field::content_type, "text/plain");
    res->body() = "404 Not Found";
    res->prepare_payload();
    return res;
  }

  std::ifstream fs(path, std::ios::binary | std::ios::ate);
  if (!fs.is_open()) {
    BOOST_LOG_TRIVIAL(error) << "Cannot open file: " << path;
    res->result(http::status::internal_server_error);
    res->set(http::field::content_type, "text/plain");
    res->body() = "500 Internal Server Error";
    res->prepare_payload();
    return res;
  }

  std::streamsize size = fs.tellg();
  fs.seekg(0, std::ios::beg);
  std::string body(size, '\0');
  fs.read(body.data(), size);
  fs.close();

  // 3. build response
  std::string ext;
  size_t dot = path.find_last_of('.');
  if (dot != std::string::npos)
    ext = path.substr(dot);

  res->result(http::status::ok);
  res->set(http::field::content_type, get_mime_type(ext));
  res->body() = std::move(body);
  res->prepare_payload();
  return res;
}

REGISTER_HANDLER("StaticHandler", static_handler)
