#include <boost/log/trivial.hpp>
#include <filesystem>
#include <optional>

#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm.h"
#include "handler_factory.h"
#include "handlers/markdown_handler.h"
#include "real_file_system.h"

using namespace wasd::http;
namespace http = boost::beast::http;
namespace fs = std::filesystem;
// Helper to create common error responses for MarkdownHandler
std::unique_ptr<Response> create_markdown_error_response(http::status status_code,
                                                         unsigned http_version,
                                                         const std::string &message) {
  auto res = std::make_unique<Response>();
  res->result(status_code);
  res->version(http_version);
  res->set(http::field::content_type, "text/plain");
  res->body() = message;
  res->prepare_payload();
  return res;
}

// Read an entire file (≤ 1 MB) into a string; return empty optional on failure
static std::optional<std::string> read_small_file(const std::shared_ptr<FileSystemInterface> &fs,
                                                  const std::string &path) {
  constexpr std::size_t MAX_SIZE = 1 * 1024 * 1024; // 1 MB
  if (!fs || !fs->file_exists(path))
    return std::nullopt;
  auto data = fs->read_file(path);
  if (!data || data->size() > MAX_SIZE)
    return std::nullopt;
  return data;
}

// Default constructor
markdown_handler::markdown_handler()
    : RequestHandler(), location_path_("./"), configured_root_("./"), template_path_(""),
      fs_(std::make_shared<RealFileSystem>()) {
  BOOST_LOG_TRIVIAL(debug) << "MarkdownHandler: Default constructor called, fs_ initialized.";
  static bool extensions_registered = false;
  if (!extensions_registered) {
    cmark_gfm_core_extensions_ensure_registered();
    extensions_registered = true;
  }
}
// Constructor used by the factory method
markdown_handler::markdown_handler(const std::string &location_path,
                                   const std::string &configured_root,
                                   const std::string &template_path,
                                   std::shared_ptr<FileSystemInterface> fs)
    : RequestHandler(), location_path_(location_path), configured_root_(configured_root),
      template_path_(template_path), fs_(fs) {
  BOOST_LOG_TRIVIAL(info) << "MarkdownHandler instance created for location: " << location_path_
                          << " with root: " << configured_root_
                          << " and template: " << template_path_ << " using "
                          << (fs_ ? "provided FileSystemInterface"
                                  : "null FileSystemInterface (this is unexpected)");
  static bool extensions_registered = false;
  if (!extensions_registered) {
    cmark_gfm_core_extensions_ensure_registered();
    extensions_registered = true;
  }
}
// Static factory method
std::unique_ptr<RequestHandler> markdown_handler::create(const std::string &location_path,
                                                         const std::string &configured_root,
                                                         const std::string &template_path,
                                                         std::shared_ptr<FileSystemInterface> fs) {
  return std::make_unique<markdown_handler>(location_path, configured_root, template_path, fs);
}
std::unique_ptr<Response> markdown_handler::handle_request(const Request &req) {
  BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Handling request for target: "
                          << std::string(req.target());
  if (!fs_) { // Should not happen if factory is used correctly
    BOOST_LOG_TRIVIAL(error)
        << "MarkdownHandler: FileSystemInterface (fs_) is null. This is a critical error.";
    return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                          "Internal Server Error: File system not available.");
  }

  // 0. Parse query string
  std::string target_full(req.target());             // e.g.  "/docs/guide.md?raw=1"
  const auto qpos        = target_full.find('?');

  // Path that we’ll use for all path‑sanitisation logic
  std::string target_path = target_full.substr(0, qpos);   //  "/docs/guide.md"

  // Everything after the '?', or empty if no query
  std::string query       = (qpos == std::string::npos) ? "" 
                                                        : target_full.substr(qpos + 1);
  // Detect ?raw=1 (allow it to be anywhere in the query string)
  bool raw_requested = (query.find("raw=1") != std::string::npos);


  // 1. Resolve the requested file path relative to the handler's location_path_
  std::string relative_path_in_docs;
  std::string comparable_location_path = location_path_;
  if (comparable_location_path != "/" &&
      (comparable_location_path.empty() || comparable_location_path.back() != '/')) {
    comparable_location_path += '/';
  }
  if (location_path_ == "/") {
    if (target_path.rfind("/", 0) == 0 && target_path.length() > 1) {
      relative_path_in_docs = target_path.substr(1);
    } else if (target_path == "/") {
      relative_path_in_docs = "";
    } else {
      relative_path_in_docs = target_path;
    }
  } else if (target_path.rfind(comparable_location_path, 0) == 0) {
    relative_path_in_docs = target_path.substr(comparable_location_path.length());
  } else if (target_path == location_path_) {
    relative_path_in_docs = "";
  } else {
    BOOST_LOG_TRIVIAL(warning) << "MarkdownHandler: Request target '" << target_path
                               << "' does not properly align with location_path '" << location_path_
                               << "'.";
    return create_markdown_error_response(http::status::not_found, req.version(),
                                          "404 Not Found - Path mismatch");
  }
  if (relative_path_in_docs.empty() || relative_path_in_docs.back() == '/') {
    relative_path_in_docs += "index.md";
  }
  fs::path target_fs_path = fs::path(configured_root_) / relative_path_in_docs;
  // 2. Path sanitization
  fs::path canonical_root;
  fs::path canonical_target_path;
  std::error_code ec_fs; // For std::filesystem operations
  try {
    // Check if configured_root_ exists and is a directory using std::filesystem directly
    if (!fs::exists(configured_root_, ec_fs) || !fs::is_directory(configured_root_, ec_fs)) {
      BOOST_LOG_TRIVIAL(error) << "MarkdownHandler: Configured root directory '" << configured_root_
                               << "' does not exist or is not a directory. Error: "
                               << ec_fs.message();
      return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                            "Internal Server Error - Invalid root configuration");
    }
    canonical_root = fs::canonical(configured_root_, ec_fs);
    if (ec_fs)
      throw fs::filesystem_error("Canonicalization of root failed", ec_fs);
    canonical_target_path = fs::weakly_canonical(target_fs_path, ec_fs);
    if (ec_fs) { // If weakly_canonical fails, it might be because the file doesn't exist.
      BOOST_LOG_TRIVIAL(debug) << "MarkdownHandler: weakly_canonical failed for target '"
                               << target_fs_path.string() << "': " << ec_fs.message()
                               << ". Using lexically_normal.";
      canonical_target_path = target_fs_path.lexically_normal(); // Fallback to normalized path
    }
  } catch (const fs::filesystem_error &e) {
    BOOST_LOG_TRIVIAL(error) << "MarkdownHandler: Filesystem error during path canonicalization: "
                             << e.what();
    return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                          "Internal Server Error - Path processing failed");
  }
  if (!canonical_root.empty()) { // Ensure canonical_root was successfully obtained
    std::string root_str = canonical_root.string();
    std::string target_str = canonical_target_path.string();
    if (target_str.rfind(root_str, 0) != 0) {
      BOOST_LOG_TRIVIAL(warning) << "MarkdownHandler: Path traversal attempt or invalid path. "
                                 << "Requested: " << target_fs_path.string()
                                 << ", Processed Target: " << target_str
                                 << ", Canonical Root: " << root_str;
      return create_markdown_error_response(http::status::not_found, req.version(),
                                            "404 Not Found - Invalid path");
    }
  } else {
    BOOST_LOG_TRIVIAL(error)
        << "MarkdownHandler: Could not obtain canonical root path for security check.";
    return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                          "Internal Server Error - Configuration issue");
  }
  if (target_fs_path.extension() != ".md") {
    BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Requested file is not a .md file: "
                            << target_fs_path.string();
    return create_markdown_error_response(http::status::not_found, req.version(),
                                          "404 Not Found - Not a Markdown file");
  }
  std::string final_file_path_str = canonical_target_path.string();
  // 3. Check file existence and type using FileSystemInterface (fs_) and std::filesystem
  if (!fs_->file_exists(final_file_path_str) || !fs::is_regular_file(final_file_path_str, ec_fs)) {
    BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Markdown file not found or not a regular file: "
                            << final_file_path_str << (ec_fs ? " Error: " + ec_fs.message() : "");
    return create_markdown_error_response(http::status::not_found, req.version(),
                                          "404 Not Found - File does not exist");
  }
  // 4. File Size Limit (1MB) using std::filesystem
  const uintmax_t MAX_FILE_SIZE = 1 * 1024 * 1024; // 1MB
  uintmax_t file_size = fs::file_size(final_file_path_str, ec_fs);
  if (ec_fs) {
    BOOST_LOG_TRIVIAL(error) << "MarkdownHandler: Could not get file size for: "
                             << final_file_path_str << ". Error: " << ec_fs.message();
    return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                          "Internal Server Error - Could not determine file size");
  }
  if (file_size > MAX_FILE_SIZE) {
    BOOST_LOG_TRIVIAL(warning) << "MarkdownHandler: File exceeds 1MB limit: " << final_file_path_str
                               << ", Size: " << file_size;
    return create_markdown_error_response(http::status::payload_too_large, req.version(),
                                          "413 Payload Too Large - File exceeds 1MB limit");
  }
  if (file_size == 0) {
    BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Markdown file is empty: " << final_file_path_str;
    auto res = std::make_unique<Response>();
    res->result(http::status::ok);
    res->version(req.version());
    res->set(http::field::content_type, "text/html");
    res->body() = "";
    res->prepare_payload();
    return res;
  }
  // 5. Read the Markdown file content using FileSystemInterface (fs_)
  auto markdown_content_opt = fs_->read_file(final_file_path_str);
  if (!markdown_content_opt) {
    BOOST_LOG_TRIVIAL(error) << "MarkdownHandler: Failed to read Markdown file: "
                             << final_file_path_str;
    return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                          "Internal Server Error - Could not read file");
  }
  std::string markdown_input = markdown_content_opt.value();
  
  // ---------- 6a.  Raw‑mode early‑return ----------
  if (raw_requested) {
    auto res = std::make_unique<Response>();
    res->result(http::status::ok);
    res->version(req.version());
    res->set(http::field::content_type, "text/markdown");
    res->body() = std::move(markdown_input);
    res->prepare_payload();

    BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Served RAW " << final_file_path_str;
    return res;
  }

  /* ---------- 6. Convert Markdown to HTML fragment ---------- */
  int cmark_options = CMARK_OPT_DEFAULT;
  char *html_output_c_str =
      cmark_markdown_to_html(markdown_input.c_str(), markdown_input.length(), cmark_options);
  if (!html_output_c_str) {
    BOOST_LOG_TRIVIAL(error) << "MarkdownHandler: cmark_markdown_to_html failed for "
                             << final_file_path_str;
    return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                          "Internal Server Error - Markdown conversion failed");
  }
  std::string html_fragment(html_output_c_str);
  free(html_output_c_str);

  /* ---------- 7. Load wrapper template & inject content ---------- */
  std::string full_page;
  bool wrapped = false;
  if (!template_path_.empty()) {
    auto tpl_opt = read_small_file(fs_, template_path_);
    if (tpl_opt) {
      full_page = *tpl_opt;
      const std::string placeholder = "{{content}}";
      std::size_t pos = full_page.find(placeholder);
      if (pos != std::string::npos) {
        full_page.replace(pos, placeholder.length(), html_fragment);
        wrapped = true;
      }
    }
  }
  if (!wrapped) {
    /* Fallback: no template or placeholder missing */
    full_page = html_fragment;
  }

  /* ---------- 8. Build the HTTP response ---------- */
  auto res = std::make_unique<Response>();
  res->result(http::status::ok);
  res->version(req.version());
  res->set(http::field::content_type, "text/html");
  res->body() = std::move(full_page);
  res->prepare_payload();

  BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Served " << final_file_path_str
                          << (wrapped ? " (wrapped)" : " (raw)");
  return res;
}

REGISTER_HANDLER("MarkdownHandler", markdown_handler)