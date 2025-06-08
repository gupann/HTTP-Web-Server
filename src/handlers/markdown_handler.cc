#include <boost/log/trivial.hpp>
#include <filesystem>
#include <optional>

#include "cmark-gfm-core-extensions.h"
#include "cmark-gfm.h"
#include "handler_factory.h"
#include "handlers/markdown_handler.h"
#include "real_file_system.h"

#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>

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

/* ------------------------------------------------------------------ */
/*  In‑memory cache for directory listings (TTL = 5 s)                */
/* ------------------------------------------------------------------ */
struct DirCacheEntry {
  std::string html;          // rendered + wrapped page
  std::string etag;          // strong ETag for the page
  std::string last_modified; // HTTP-date for directory mtime
  std::chrono::steady_clock::time_point saved;
};
static std::unordered_map<std::string, DirCacheEntry> g_dir_cache;
static constexpr std::chrono::seconds kDirCacheTTL{5};
static std::mutex g_dir_cache_mu; // simple thread‑safety

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

// Convert fs::file_time_type → RFC‑1123 string for HTTP headers
static std::string http_date_from_fs_time(fs::file_time_type tp) {
  auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      tp - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
  std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
  std::tm gmt = *std::gmtime(&tt);

  std::ostringstream os;
  os << std::put_time(&gmt, "%a, %d %b %Y %H:%M:%S GMT");
  return os.str();
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

/* --- utility ----------------------------------------------------------- */
static std::string render_markdown_gfm(const std::string &md) {
  /* 1. create a GFM-capable parser */
  int options = CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE; // allow raw HTML
  cmark_parser *parser = cmark_parser_new(options);

  /* 2. attach desired extensions */
  const char *exts[] = {"table",    "strikethrough", "autolink", "tagfilter",
                        "tasklist", "strikethrough", nullptr};
  for (const char **e = exts; *e; ++e) {
    if (auto *ext = cmark_find_syntax_extension(*e))
      cmark_parser_attach_syntax_extension(parser, ext);
  }

  /* 3. feed input & get AST */
  cmark_parser_feed(parser, md.c_str(), md.size());
  cmark_node *doc = cmark_parser_finish(parser);

  /* 4. render HTML with the same extension set */
  const cmark_llist *ext_list_const = cmark_parser_get_syntax_extensions(parser);
  cmark_llist *ext_list = const_cast<cmark_llist *>(ext_list_const);

  char *html = cmark_render_html(doc, options, ext_list);

  std::string out(html ? html : "");
  free(html);
  cmark_node_free(doc);
  cmark_parser_free(parser);
  return out;
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
  std::string target_full(req.target()); // e.g.  "/docs/guide.md?raw=1"
  const auto qpos = target_full.find('?');

  // Path that we’ll use for all path‑sanitisation logic
  std::string target_path = target_full.substr(0, qpos); //  "/docs/guide.md"

  // Everything after the '?', or empty if no query
  std::string query = (qpos == std::string::npos) ? "" : target_full.substr(qpos + 1);
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
  // if (relative_path_in_docs.empty() || relative_path_in_docs.back() == '/') {
  //   relative_path_in_docs += "index.md";
  // }
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

  /* ================================================================== */
  /*  D‑1: Handle requests that resolve to a *directory*                */
  /* ================================================================== */
  bool is_dir_request = fs::is_directory(canonical_target_path, ec_fs);
  if (is_dir_request) {

    /* 1 ─ Ensure trailing “/”; if missing, 301 redirect */
    if (!target_path.empty() && target_path.back() != '/') {
      auto res = std::make_unique<Response>();
      res->result(http::status::moved_permanently);
      res->version(req.version());
      res->set(http::field::location, target_path + "/");
      res->prepare_payload();
      return res;
    }

    // 2 ─ Try cache & conditional‐GET
    const std::string canon_dir = canonical_target_path.string();
    const auto now = std::chrono::steady_clock::now();

    {
      std::lock_guard<std::mutex> lk(g_dir_cache_mu);
      auto it = g_dir_cache.find(canon_dir);
      if (it != g_dir_cache.end() && now - it->second.saved < kDirCacheTTL) {
        auto &entry = it->second;
        // If‐None‐Match
        if (req.find(http::field::if_none_match) != req.end() &&
            req.at(http::field::if_none_match) == entry.etag) {
          auto res = std::make_unique<Response>();
          res->result(http::status::not_modified);
          res->version(req.version());
          res->set(http::field::etag, entry.etag);
          res->set(http::field::last_modified, entry.last_modified);
          return res;
        }
        // If‐Modified‐Since
        if (req.find(http::field::if_modified_since) != req.end() &&
            req.at(http::field::if_modified_since) == entry.last_modified) {
          auto res = std::make_unique<Response>();
          res->result(http::status::not_modified);
          res->version(req.version());
          res->set(http::field::etag, entry.etag);
          res->set(http::field::last_modified, entry.last_modified);
          return res;
        }
        // Serve cached
        auto res = std::make_unique<Response>();
        res->result(http::status::ok);
        res->version(req.version());
        res->set(http::field::content_type, "text/html");
        res->set(http::field::etag, entry.etag);
        res->set(http::field::last_modified, entry.last_modified);
        res->body() = entry.html;
        res->prepare_payload();
        return res;
      }
    }

    // 3 ─ Scan directory for .md and directories, build the <ul>…
    std::vector<std::string> md_files;
    std::vector<std::string> sub_directories;

    // fs::directory_iterator sets ec_fs if canonical_target_path cannot be opened.
    for (const auto &p : fs::directory_iterator(canonical_target_path, ec_fs)) {
      if (p.is_regular_file() && p.path().extension() == ".md") {
        md_files.push_back(p.path().filename().string());
      } else if (p.is_directory()) {
        sub_directories.push_back(p.path().filename().string());
      }
    }

    if (ec_fs) {
      BOOST_LOG_TRIVIAL(error) << "MarkdownHandler: Error during directory iteration: "
                               << ec_fs.message();
      return create_markdown_error_response(http::status::internal_server_error, req.version(),
                                            "Internal Server Error - Directory iteration failed");
    }

    std::sort(sub_directories.begin(), sub_directories.end()); // Sort directories
    std::sort(md_files.begin(), md_files.end());               // Sort markdown files

    std::ostringstream list_html;
    list_html << "<h1>Index of " << target_path << "</h1>\n<ul>\n";

    // List directories first
    for (const auto &dir_name : sub_directories) {
      list_html << "  <li><a href=\"" << dir_name << "/" << "\">" << dir_name << "/"
                << "</a></li>\n";
    }

    // Then list markdown files
    for (const auto &f : md_files) {
      list_html << "  <li><a href=\"" << f << "\">" << f << "</a></li>\n";
    }
    list_html << "</ul>\n";

    /* 4 ─ Wrap with template (reuse existing logic) */
    std::string full_page = list_html.str();
    bool wrapped = false;
    if (!template_path_.empty()) {
      auto tpl_opt = read_small_file(fs_, template_path_);
      if (tpl_opt) {
        full_page = *tpl_opt;
        const std::string placeholder = "{{content}}";
        auto pos = full_page.find(placeholder);
        if (pos != std::string::npos) {
          full_page.replace(pos, placeholder.length(), list_html.str());
          wrapped = true;
        }
      }
    }
    if (!wrapped) {
      full_page = list_html.str();
    }

    // 5 ─ Generate ETag & Last-Modified & save to cache
    std::string etag =
        "\"" + std::to_string(full_page.size()) + "-" +
        std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count()) +
        "\"";
    auto dir_mtime = fs::last_write_time(canonical_target_path, ec_fs);
    std::string last_modified = http_date_from_fs_time(dir_mtime);

    {
      std::lock_guard<std::mutex> lk(g_dir_cache_mu);
      g_dir_cache[canon_dir] = {full_page, etag, last_modified, now};
    }

    /* 6 ─ Return response */
    auto res = std::make_unique<Response>();
    res->result(http::status::ok);
    res->version(req.version());
    res->set(http::field::content_type, "text/html");
    res->set(http::field::etag, etag);
    res->set(http::field::last_modified, last_modified);
    res->body() = std::move(full_page);
    res->prepare_payload();
    return res;
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
  /* ---------- File‑metadata for caching ---------- */
  auto file_mtime = fs::last_write_time(final_file_path_str, ec_fs);
  if (ec_fs) { /*  existing error branch is fine  */
  }

  uintmax_t file_size = fs::file_size(final_file_path_str, ec_fs);

  // strong ETag: "<size>-<mtime_epoch>"
  auto mtime_secs =
      std::chrono::time_point_cast<std::chrono::seconds>(file_mtime).time_since_epoch().count();
  std::string etag = "\"" + std::to_string(file_size) + "-" + std::to_string(mtime_secs) + "\"";
  std::string last_modified = http_date_from_fs_time(file_mtime);

  /* ---------- Conditional‑GET handling ---------- */
  bool send_304 = false;

  if (req.find(http::field::if_none_match) != req.end()) {
    if (req.at(http::field::if_none_match) == etag)
      send_304 = true;
  } else if (req.find(http::field::if_modified_since) != req.end()) {
    if (req.at(http::field::if_modified_since) == last_modified)
      send_304 = true;
  }

  if (send_304) {
    auto res = std::make_unique<Response>();
    res->result(http::status::not_modified); // 304
    res->version(req.version());
    res->set(http::field::etag, etag);
    res->set(http::field::last_modified, last_modified);
    return res; // ── early exit
  }

  // 4. File Size Limit (1MB) using std::filesystem
  const uintmax_t MAX_FILE_SIZE = 1 * 1024 * 1024; // 1MB
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
    res->set(http::field::etag, etag);
    res->set(http::field::last_modified, last_modified);
    res->prepare_payload();

    BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Served RAW " << final_file_path_str;
    return res;
  }

  /* ---------- 6. Convert Markdown to HTML fragment ---------- */
  std::string html_fragment = render_markdown_gfm(markdown_input);

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
  res->set(http::field::etag, etag);
  res->set(http::field::last_modified, last_modified);
  res->prepare_payload();

  BOOST_LOG_TRIVIAL(info) << "MarkdownHandler: Served " << final_file_path_str
                          << (wrapped ? " (wrapped)" : " (raw)");
  return res;
}

REGISTER_HANDLER("MarkdownHandler", markdown_handler)