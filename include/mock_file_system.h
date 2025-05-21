#ifndef MOCK_FILE_SYSTEM_H
#define MOCK_FILE_SYSTEM_H

#include <unordered_map>
#include "file_system.h"

namespace wasd::http {

// Mock filesystem for testing
class MockFileSystem : public FileSystemInterface {
public:
  // Mock storage for our "files"
  std::unordered_map<std::string, std::string> mock_files;
  std::unordered_map<std::string, std::vector<std::string>> mock_directories;

  bool file_exists(const std::string &path) const override;

  std::optional<std::string> read_file(const std::string &path) const override;

  bool write_file(const std::string &path, const std::string &content) override;

  bool delete_file(const std::string &path) override;
  bool create_directory(const std::string &path) override;

  std::vector<std::string> list_directory(const std::string &path) const override;
};

} // namespace wasd::http

#endif