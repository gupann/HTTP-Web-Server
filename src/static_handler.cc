#include "static_handler.h"
#include <boost/log/trivial.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>

using namespace wasd::http;

namespace http = boost::beast::http;

// Basic URL Decoder (Handles %20 -> space, + -> space)
std::string url_decode_simple(const std::string &encoded) {
  std::string decoded;
  decoded.reserve(encoded.length());
  for (size_t i = 0; i < encoded.length(); ++i) {
    if (encoded[i] == '%' && i + 2 < encoded.length() && encoded[i + 1] == '2' &&
        encoded[i + 2] == '0') {
      decoded += ' ';
      i += 2;
    } else if (encoded[i] == '+') {
      decoded += ' ';
    } else {
      decoded += encoded[i];
    }
  }
  return decoded;
}

// Function to determine MIME type based on file extension (needs to be accessible)
std::string get_mime_type(const std::string &file_extension) {
  static const std::unordered_map<std::string, std::string> mime_map = {
      {".html", "text/html"},      {".htm", "text/html"},
      {".css", "text/css"},        {".js", "application/javascript"},
      {".jpeg", "image/jpeg"},     {".jpg", "image/jpeg"},
      {".png", "image/png"},       {".gif", "image/gif"},
      {".txt", "text/plain"},      // Required by assignment
      {".zip", "application/zip"}, // Required by assignment
      {".pdf", "application/pdf"}, {".ico", "image/x-icon"},
      {".svg", "image/svg+xml"}};

  std::string ext_lower = file_extension;
  std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(), ::tolower);

  auto it = mime_map.find(ext_lower);
  if (it != mime_map.end()) {
    return it->second;
  } else {
    return "application/octet-stream"; // Default for unknown types
  }
}

void static_handler::handle_request(http::request<http::string_body> &req,
                                    http::response<http::string_body> &res) {
  const std::string url_prefix = get_prefix();
  const std::string root_dir = get_dir();
  std::string decoded_target = url_decode_simple(req.target());

  if (decoded_target.rfind(url_prefix, 0) != 0) {
    // This handler wasn't the correct match for the request path.
    // This case should ideally be handled by the routing logic before calling this handler.
    // If it reaches here, it's likely a configuration or routing error. Return 404 or 500.
    BOOST_LOG_TRIVIAL(error) << "Static handler called for wrong prefix. Target: " << decoded_target
                             << ", Prefix: " << url_prefix;
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.version(req.version());
    res.body() = "404 Not Found: Resource prefix mismatch.";
    res.prepare_payload();
    return;
  }

  // Extract the relative path and construct the full filesystem path
  std::string relative_path = decoded_target.substr(url_prefix.length());
  std::string file_path = root_dir;

  // Ensure root_dir doesn't end with '/' and relative_path starts with '/'
  if (file_path.length() > 1 && file_path.back() == '/') {
    file_path.pop_back();
  }
  if (relative_path.empty() || relative_path[0] != '/') {
    relative_path = "/" + relative_path;
  }
  file_path += relative_path;

  // Basic Path Sanitization (Prevent '..')
  if (file_path.find("..") != std::string::npos || file_path.rfind(root_dir, 0) != 0) {
    BOOST_LOG_TRIVIAL(warning) << "Potential directory traversal blocked for path: " << file_path
                               << " (Relative: " << relative_path << ")";
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.version(req.version());
    res.body() = "404 Not Found: Invalid path component.";
    res.prepare_payload();
    return;
  }
  BOOST_LOG_TRIVIAL(info) << "Serving request for '" << decoded_target << "' -> '" << file_path
                          << "'";

  std::error_code ec;
  bool is_file = std::filesystem::is_regular_file(file_path, ec);
  // Use binary mode for all file types
  std::ifstream file_stream(file_path, std::ios::binary | std::ios::ate);

  if (!is_file || !file_stream.is_open()) {
    // File not found or inaccessible
    BOOST_LOG_TRIVIAL(warning) << "File not found or is not valid format: " << file_path;
    res.result(http::status::not_found);
    res.set(http::field::content_type, "text/plain");
    res.version(req.version());
    res.body() = "404 Not Found: The requested file could not be found.";
    res.prepare_payload();
    return;
  }

  // Get size and read content
  std::streamsize size = file_stream.tellg();
  file_stream.seekg(0, std::ios::beg);

  std::string file_content(size, '\0'); // Efficiently preallocate string
  if (!file_stream.read(&file_content[0], size)) {
    BOOST_LOG_TRIVIAL(error) << "Failed to read file content: " << file_path;
    res.result(http::status::internal_server_error);
    res.set(http::field::content_type, "text/plain");
    res.version(req.version());
    res.body() = "500 Internal Server Error: Could not read file.";
    res.prepare_payload();
    return;
  }
  file_stream.close();

  // Determine MIME type based on extension
  std::string extension;
  size_t dot_pos = file_path.find_last_of(".");
  if (dot_pos != std::string::npos) {
    extension = file_path.substr(dot_pos);
  }
  std::string mime_type = get_mime_type(extension);

  res.result(http::status::ok);
  res.version(req.version());
  res.set(http::field::content_type, mime_type);
  res.body() = std::move(file_content);
  res.prepare_payload();
}