#pragma once
#include <memory>
#include <string>
#include "config_parser.h"
#include "real_file_system.h"
#include "request_handler.h"
namespace wasd::http {
class markdown_handler : public RequestHandler {
public:
  // Constructor that takes parsed configuration
  markdown_handler();
  markdown_handler(const std::string &location_path, const std::string &configured_root,
                   const std::string &template_path, std::shared_ptr<RealFileSystem> fs);
  // Static factory method - to be called by HandlerRegistry
  static std::unique_ptr<RequestHandler> create(const std::string &location_path,
                                                const std::string &configured_root,
                                                const std::string &template_path,
                                                std::shared_ptr<RealFileSystem> fs);
  std::unique_ptr<Response> handle_request(const Request &req) override;

private:
  std::string location_path_;
  std::string configured_root_;
  std::string template_path_;
  std::shared_ptr<RealFileSystem> fs_;
};
} // namespace wasd::http