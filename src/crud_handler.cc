#include "crud_handler.h"
#include <filesystem>
#include <fstream>

using namespace wasd::http;
namespace fs = std::filesystem;
namespace http = boost::beast::http;

// RealFileSystem implementation
bool RealFileSystem::file_exists(const std::string &path) const {
  return fs::exists(path);
}

std::optional<std::string> RealFileSystem::read_file(const std::string &path) const {
  if (!file_exists(path)) {
    return std::nullopt;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  return content;
}

bool RealFileSystem::write_file(const std::string &path, const std::string &content) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return false;
  }

  file << content;
  return file.good();
}

bool RealFileSystem::delete_file(const std::string &path) {
  return fs::remove(path);
}

bool RealFileSystem::create_directory(const std::string &path) {
  return fs::create_directories(path);
}

std::vector<std::string> RealFileSystem::list_directory(const std::string &path) const {
  std::vector<std::string> files;

  if (!fs::exists(path) || !fs::is_directory(path)) {
    return files;
  }

  for (const auto &entry : fs::directory_iterator(path)) {
    if (fs::is_regular_file(entry)) {
      files.push_back(entry.path().filename().string());
    }
  }

  return files;
}

// CrudRequestHandler implementation
CrudRequestHandler::CrudRequestHandler(const std::string &data_path,
                                       std::shared_ptr<FileSystemInterface> fs)
    : data_path_(data_path), fs_(std::move(fs)) {
  // Create the root data directory if it doesn't exist
  fs_->create_directory(data_path_);
}

std::unique_ptr<Response> CrudRequestHandler::handle_request(const Request &req) {
  std::string path(req.target());
  auto [entity_type, id] = parse_path(path);

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
  // TODO: Implement POST functionality
  // 1. Validate the request body is valid JSON
  // 2. Create a directory for the entity type if it doesn't exist
  // 3. Generate a new unique ID for the entity
  // 4. Save the JSON to a file with the entity's ID
  // 5. Return the ID in the response
  return std::make_unique<Response>(http::status::not_implemented, req.version());
}

std::unique_ptr<Response> CrudRequestHandler::handle_get(const Request &req,
                                                         const std::string &entity_type,
                                                         const std::optional<std::string> &id) {
  // TODO: Implement GET functionality
  // If ID is present:
  //   1. Check if the entity exists
  //   2. Return the entity data if found
  // If ID is not present:
  //   1. List all entity IDs of the given type
  return std::make_unique<Response>(http::status::not_implemented, req.version());
}

std::unique_ptr<Response> CrudRequestHandler::handle_put(const Request &req,
                                                         const std::string &entity_type,
                                                         const std::string &id) {
  // TODO: Implement PUT functionality
  // 1. Validate the request body is valid JSON
  // 2. Create a directory for the entity type if it doesn't exist
  // 3. Update or create the entity with the specified ID
  return std::make_unique<Response>(http::status::not_implemented, req.version());
}

std::unique_ptr<Response> CrudRequestHandler::handle_delete(const Request &req,
                                                            const std::string &entity_type,
                                                            const std::string &id) {
  // TODO: Implement DELETE functionality
  // 1. Check if the entity exists
  // 2. Delete the entity if found
  return std::make_unique<Response>(http::status::not_implemented, req.version());
}

std::pair<std::string, std::optional<std::string>>
CrudRequestHandler::parse_path(const std::string &path) {
  // TODO: Implement path parsing
  // Extract entity type and optional ID from the path
  // Example: "/Shoes/1" -> {"Shoes", "1"}
  //          "/Shoes"   -> {"Shoes", std::nullopt}
  return {"", std::nullopt}; // Placeholder
}

std::string CrudRequestHandler::generate_id(const std::string &entity_type) {
  // TODO: Implement ID generation
  // Find the next available ID for the given entity type
  return "1"; // Placeholder
}

bool CrudRequestHandler::is_valid_json(const std::string &json_str) {
  // TODO: Implement JSON validation
  return true; // Placeholder
}

// REGISTER_HANDLER("CrudHandler", CrudRequestHandler)