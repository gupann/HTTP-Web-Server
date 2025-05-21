#include "mock_file_system.h"
#include <algorithm>

using namespace wasd::http;

bool MockFileSystem::file_exists(const std::string &path) const {
  return mock_files.find(path) != mock_files.end() ||
         mock_directories.find(path) != mock_directories.end();
}

std::optional<std::string> MockFileSystem::read_file(const std::string &path) const {
  auto it = mock_files.find(path);
  if (it != mock_files.end()) {
    return it->second;
  }
  return std::nullopt;
}

bool MockFileSystem::write_file(const std::string &path, const std::string &content) {
  mock_files[path] = content;

  // Extract directory from path
  size_t last_slash = path.find_last_of('/');
  if (last_slash != std::string::npos) {
    std::string dir = path.substr(0, last_slash);
    std::string filename = path.substr(last_slash + 1);

    // Ensure directory exists in our mock
    if (mock_directories.find(dir) == mock_directories.end()) {
      mock_directories[dir] = std::vector<std::string>();
    }

    // Add filename to directory if not already there
    auto &files = mock_directories[dir];
    if (std::find(files.begin(), files.end(), filename) == files.end()) {
      files.push_back(filename);
    }
  }

  return true;
}

bool MockFileSystem::delete_file(const std::string &path) {
  auto it = mock_files.find(path);
  if (it != mock_files.end()) {
    mock_files.erase(it);

    // Remove from directory listing too
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos) {
      std::string dir = path.substr(0, last_slash);
      std::string filename = path.substr(last_slash + 1);

      auto dir_it = mock_directories.find(dir);
      if (dir_it != mock_directories.end()) {
        auto &files = dir_it->second;
        files.erase(std::remove(files.begin(), files.end(), filename), files.end());
      }
    }

    return true;
  }
  return false;
}

bool MockFileSystem::create_directory(const std::string &path) {
  mock_directories[path] = std::vector<std::string>();
  return true;
}

std::vector<std::string> MockFileSystem::list_directory(const std::string &path) const {
  auto it = mock_directories.find(path);
  if (it != mock_directories.end()) {
    return it->second;
  }
  return std::vector<std::string>();
}