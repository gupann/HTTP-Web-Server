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

  // Failure simulation flags
  bool write_should_fail = false;
  bool read_should_fail = false;
  bool delete_should_fail = false;
  bool create_directory_should_fail = false;

  bool file_exists(const std::string &path) const override;

  std::optional<std::string> read_file(const std::string &path) const override;

  bool write_file(const std::string &path, const std::string &content) override;

  bool delete_file(const std::string &path) override;
  bool create_directory(const std::string &path) override;

  std::vector<std::string> list_directory(const std::string &path) const override;

  // Methods to control failure simulation
  void set_write_should_fail(bool should_fail) { write_should_fail = should_fail; }
  void set_read_should_fail(bool should_fail) { read_should_fail = should_fail; }
  void set_delete_should_fail(bool should_fail) { delete_should_fail = should_fail; }
  void set_create_directory_should_fail(bool should_fail) {
    create_directory_should_fail = should_fail;
  }
};

} // namespace wasd::http

#endif