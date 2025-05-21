#ifndef WASD_FILE_SYSTEM_H
#define WASD_FILE_SYSTEM_H

#include <optional>
#include <string>
#include <vector>

namespace wasd::http {

// Interface for file system operations to enable dependency injection for testing
class FileSystemInterface {
public:
  virtual ~FileSystemInterface() = default;

  // Check if a file exists
  virtual bool file_exists(const std::string &path) const = 0;

  // Read content from a file
  virtual std::optional<std::string> read_file(const std::string &path) const = 0;

  // Write content to a file
  virtual bool write_file(const std::string &path, const std::string &content) = 0;

  // Delete a file
  virtual bool delete_file(const std::string &path) = 0;

  // Create a directory
  virtual bool create_directory(const std::string &path) = 0;

  // List files in a directory
  virtual std::vector<std::string> list_directory(const std::string &path) const = 0;
};

} // namespace wasd::http

#endif // WASD_FILE_SYSTEM_H