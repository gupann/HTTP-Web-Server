#ifndef CRUD_REQUEST_HANDLER_H
#define CRUD_REQUEST_HANDLER_H

#include "real_file_system.h"
#include "request_handler.h"

namespace wasd::http {

class CrudRequestHandler : public RequestHandler {
public:
  CrudRequestHandler();
  CrudRequestHandler(const std::string &prefix, const std::string &data_path,
                     std::shared_ptr<FileSystemInterface> fs = std::make_shared<RealFileSystem>());

  std::unique_ptr<Response> handle_request(const Request &req) override;

  // Helper methods for CRUD operations
  std::unique_ptr<Response> handle_post(const Request &req, const std::string &entity_type);
  std::unique_ptr<Response> handle_get(const Request &req, const std::string &entity_type,
                                       const std::optional<std::string> &id);
  std::unique_ptr<Response> handle_put(const Request &req, const std::string &entity_type,
                                       const std::string &id);
  std::unique_ptr<Response> handle_delete(const Request &req, const std::string &entity_type,
                                          const std::string &id);

  // Helper method to parse URL path into entity type and ID
  std::pair<std::string, std::optional<std::string>> parse_path(const std::string &path);

private:
  // URI prefix to match
  std::string prefix_;

  // Root directory for data storage
  std::string data_path_;

  // File system interface for dependency injection
  std::shared_ptr<FileSystemInterface> fs_;

  // Helper method to generate a unique ID for new entities
  std::string generate_id(const std::string &entity_type);

  // Helper method to validate JSON data
  bool is_valid_json(const std::string &json_str);
};

} // namespace wasd::http

#endif // CRUD_REQUEST_HANDLER_H