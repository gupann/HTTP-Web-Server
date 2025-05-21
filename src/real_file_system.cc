#include "real_file_system.h"
#include <filesystem>
#include <fstream>

using namespace wasd::http;
namespace fs = std::filesystem;

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
