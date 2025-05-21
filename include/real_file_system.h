#ifndef REAL_FILE_SYSTEM_H
#define REAL_FILE_SYSTEM_H

#include "file_system.h"

namespace wasd::http {

// Real implementation of the FileSystemInterface using std::filesystem
class RealFileSystem : public FileSystemInterface {
public:
  bool file_exists(const std::string &path) const override;
  std::optional<std::string> read_file(const std::string &path) const override;
  bool write_file(const std::string &path, const std::string &content) override;
  bool delete_file(const std::string &path) override;
  bool create_directory(const std::string &path) override;
  std::vector<std::string> list_directory(const std::string &path) const override;
};

} // namespace wasd::http

#endif // REAL_FILE_SYSTEM_H