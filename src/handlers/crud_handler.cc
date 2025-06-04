#include "handlers/crud_handler.h"
#include <algorithm>
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#include <sstream>
#include "handler_factory.h"
#include "real_file_system.h"

using namespace wasd::http;
namespace http = boost::beast::http;
namespace json = boost::json;

CrudRequestHandler::CrudRequestHandler()
    : prefix_("/api"), data_path_("./data"), fs_(std::make_shared<RealFileSystem>()) {
  fs_->create_directory(data_path_);
}

CrudRequestHandler::CrudRequestHandler(const std::string &prefix, const std::string &data_path,
                                       std::shared_ptr<FileSystemInterface> fs)
    : prefix_(prefix), data_path_(data_path), fs_(std::move(fs)) {
  // Create the root data directory if it doesn't exist
  fs_->create_directory(data_path_);
}

std::unique_ptr<Response> CrudRequestHandler::handle_request(const Request &req) {
  std::string path(req.target());

  // Check if the request path starts with our prefix
  if (path.rfind(prefix_, 0) != 0) {
    // Path doesn't start with our prefix - return 404
    auto res = std::make_unique<Response>(http::status::not_found, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Not found\"}";
    res->prepare_payload();
    return res;
  }

  // Remove prefix from path before parsing
  std::string relative_path = path.substr(prefix_.size());
  auto [entity_type, id] = parse_path(relative_path);

  if (entity_type.empty()) {
    // Invalid request path
    auto res = std::make_unique<Response>(http::status::bad_request, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Invalid request path\"}";
    res->prepare_payload();
    return res;
  }

  // Handle request based on HTTP method
  switch (req.method()) {
  case http::verb::post:
    return handle_post(req, entity_type);
  case http::verb::get:
    return handle_get(req, entity_type, id);
  case http::verb::put:
    if (!id.has_value()) {
      auto res = std::make_unique<Response>(http::status::bad_request, req.version());
      res->set(http::field::content_type, "application/json");
      res->body() = "{\"error\": \"PUT requests require an ID\"}";
      res->prepare_payload();
      return res;
    }
    return handle_put(req, entity_type, id.value());
  case http::verb::delete_:
    if (!id.has_value()) {
      auto res = std::make_unique<Response>(http::status::bad_request, req.version());
      res->set(http::field::content_type, "application/json");
      res->body() = "{\"error\": \"DELETE requests require an ID\"}";
      res->prepare_payload();
      return res;
    }
    return handle_delete(req, entity_type, id.value());
  default:
    auto res = std::make_unique<Response>(http::status::method_not_allowed, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Method not allowed\"}";
    res->prepare_payload();
    return res;
  }
}

std::unique_ptr<Response> CrudRequestHandler::handle_post(const Request &req,
                                                          const std::string &entity_type) {
  // Validate request body is not empty
  if (req.body().empty()) {
    auto res = std::make_unique<Response>(http::status::bad_request, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Request body cannot be empty\"}";
    res->prepare_payload();
    return res;
  }

  // Check Content-Type header
  auto ct = req[http::field::content_type];
  if (!ct.empty() && ct != "application/json") {
    auto res = std::make_unique<Response>(http::status::unsupported_media_type, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Content-Type must be application/json\"}";
    res->prepare_payload();
    return res;
  }

  // Validate JSON
  if (!is_valid_json(req.body())) {
    auto res = std::make_unique<Response>(http::status::bad_request, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Invalid JSON\"}";
    res->prepare_payload();
    return res;
  }

  // Create directory for entity type if it doesn't exist
  std::string entity_dir = data_path_ + "/" + entity_type;
  fs_->create_directory(entity_dir);

  // Generate new unique ID
  std::string new_id = generate_id(entity_type);
  std::string entity_path = entity_dir + "/" + new_id;

  // Save the JSON data
  if (!fs_->write_file(entity_path, req.body())) {
    auto res = std::make_unique<Response>(http::status::internal_server_error, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Failed to save entity\"}";
    res->prepare_payload();
    return res;
  }

  // Create response with the new ID
  auto res = std::make_unique<Response>(http::status::created, req.version());
  res->set(http::field::content_type, "application/json");

  // Set Location header
  std::stringstream loc;
  loc << prefix_ << "/" << entity_type << "/" << new_id;
  res->set(http::field::location, loc.str());

  // Return JSON with the ID
  json::object response_body;
  response_body["id"] = std::stoi(new_id);
  std::string body_str = json::serialize(response_body);
  res->body() = body_str;
  res->prepare_payload();
  return res;
}

std::unique_ptr<Response> CrudRequestHandler::handle_get(const Request &req,
                                                         const std::string &entity_type,
                                                         const std::optional<std::string> &id) {
  // Create a response with the same HTTP version as the request
  auto res = std::make_unique<Response>(http::status::ok, req.version());
  res->set(http::field::content_type, "application/json");

  // Create directory path for the entity type
  std::string entity_dir = data_path_ + "/" + entity_type;

  // Case 1: Request for a specific entity by ID
  if (id.has_value()) {
    std::string entity_path = entity_dir + "/" + id.value();

    // Check if the entity file exists
    if (!fs_->file_exists(entity_path)) {
      // Entity not found
      res->result(http::status::not_found);
      res->body() = "{\"error\": \"Entity not found\"}";
      res->prepare_payload();
      return res;
    }

    // Read the entity data
    auto data = fs_->read_file(entity_path);
    if (!data.has_value()) {
      // File exists but couldn't be read
      res->result(http::status::internal_server_error);
      res->body() = "{\"error\": \"Failed to read entity data\"}";
      res->prepare_payload();
      return res;
    }

    // Return the entity data
    res->body() = data.value();
    res->prepare_payload();
    return res;
  }

  // Case 2: Request to list all entity IDs
  // Build a JSON array of IDs
  std::string json_array = "[";
  if (fs_->file_exists(entity_dir)) {
    // Get list of files in the entity directory
    std::vector<std::string> files = fs_->list_directory(entity_dir);
    std::sort(files.begin(), files.end());

    for (size_t i = 0; i < files.size(); ++i) {
      json_array += "\"" + files[i] + "\"";
      if (i < files.size() - 1) {
        json_array += ", ";
      }
    }
  }
  json_array += "]";

  // Return the list of IDs
  res->body() = json_array;
  res->prepare_payload();
  return res;
}

std::unique_ptr<Response> CrudRequestHandler::handle_put(const Request &req,
                                                         const std::string &entity_type,
                                                         const std::string &id) {
  // Validate request body is not empty
  if (req.body().empty()) {
    auto res = std::make_unique<Response>(http::status::bad_request, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Request body cannot be empty\"}";
    res->prepare_payload();
    return res;
  }

  // Check Content-Type header
  auto ct = req[http::field::content_type];
  if (!ct.empty() && ct != "application/json") {
    auto res = std::make_unique<Response>(http::status::unsupported_media_type, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Content-Type must be application/json\"}";
    res->prepare_payload();
    return res;
  }

  // Validate JSON
  if (!is_valid_json(req.body())) {
    auto res = std::make_unique<Response>(http::status::bad_request, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Invalid JSON\"}";
    res->prepare_payload();
    return res;
  }

  // Create directory for entity type if it doesn't exist
  std::string entity_dir = data_path_ + "/" + entity_type;
  fs_->create_directory(entity_dir);

  std::string entity_path = entity_dir + "/" + id;

  // Check if entity already exists
  bool existed = fs_->file_exists(entity_path);

  // Save/update the entity
  if (!fs_->write_file(entity_path, req.body())) {
    auto res = std::make_unique<Response>(http::status::internal_server_error, req.version());
    res->set(http::field::content_type, "application/json");
    res->body() = "{\"error\": \"Failed to save entity\"}";
    res->prepare_payload();
    return res;
  }

  if (existed) {
    // Updated existing entity - return 204 No Content
    auto res = std::make_unique<Response>(http::status::no_content, req.version());
    res->prepare_payload();
    return res;
  } else {
    // Created new entity - return 201 Created with Location header
    auto res = std::make_unique<Response>(http::status::created, req.version());
    std::stringstream loc;
    loc << prefix_ << "/" << entity_type << "/" << id;
    res->set(http::field::location, loc.str());
    res->prepare_payload();
    return res;
  }
}

std::unique_ptr<Response> CrudRequestHandler::handle_delete(const Request &req,
                                                            const std::string &entity_type,
                                                            const std::string &id) {
  // Create a response with the same HTTP version as the request
  auto res = std::make_unique<Response>(http::status::ok, req.version());
  res->set(http::field::content_type, "application/json");

  // Create directory path for the entity type
  std::string entity_dir = data_path_ + "/" + entity_type;
  std::string entity_path = entity_dir + "/" + id;

  // Check if the entity file exists
  if (!fs_->file_exists(entity_path)) {
    // Entity not found - return 404
    res->result(http::status::not_found);
    res->body() = "{\"error\": \"Entity not found\"}";
    res->prepare_payload();
    return res;
  }

  // Try to delete the entity
  if (!fs_->delete_file(entity_path)) {
    // Failed to delete - return 500
    res->result(http::status::internal_server_error);
    res->body() = "{\"error\": \"Failed to delete entity\"}";
    res->prepare_payload();
    return res;
  }

  // Successful deletion - return 204 No Content
  res->result(http::status::no_content);
  res->body() = "";
  res->prepare_payload();
  return res;
}

std::pair<std::string, std::optional<std::string>>
CrudRequestHandler::parse_path(const std::string &path) {
  // Skip the leading slash if present
  size_t start_pos = (path[0] == '/') ? 1 : 0;

  // If path is empty or only has a slash, return empty result
  if (start_pos >= path.length()) {
    return {"", std::nullopt};
  }

  // Find the next slash after the entity type
  size_t id_separator = path.find('/', start_pos);

  // Extract entity type
  std::string entity_type;
  if (id_separator == std::string::npos) {
    entity_type = path.substr(start_pos);
  } else {
    entity_type = path.substr(start_pos, id_separator - start_pos);
  }

  // If entity_type is empty, return empty result
  if (entity_type.empty()) {
    return {"", std::nullopt};
  }

  // If we found a slash and there's content after it, extract the ID
  if (id_separator != std::string::npos && id_separator + 1 < path.length()) {
    // Check if there are more slashes (invalid format)
    if (path.find('/', id_separator + 1) != std::string::npos) {
      return {"", std::nullopt}; // Invalid path format (too many segments)
    }
    return {entity_type, path.substr(id_separator + 1)};
  }

  // No ID component
  return {entity_type, std::nullopt};
}

std::string CrudRequestHandler::generate_id(const std::string &entity_type) {
  std::string entity_dir = data_path_ + "/" + entity_type;

  // If directory doesn't exist, start with ID 1
  if (!fs_->file_exists(entity_dir)) {
    return "1";
  }

  // Get list of existing files (IDs) and find the next available ID
  std::vector<std::string> files = fs_->list_directory(entity_dir);
  std::vector<int> ids;

  for (const auto &file : files) {
    try {
      int id = std::stoi(file);
      if (id > 0) { // Only consider positive integer IDs
        ids.push_back(id);
      }
    } catch (const std::exception &) {
      // Ignore non-numeric filenames
    }
  }

  if (ids.empty()) {
    return "1";
  }

  // Find the maximum ID and return the next one
  int max_id = *std::max_element(ids.begin(), ids.end());
  return std::to_string(max_id + 1);
}

bool CrudRequestHandler::is_valid_json(const std::string &json_str) {
  try {
    json::parse(json_str);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

REGISTER_HANDLER("CrudHandler", CrudRequestHandler)