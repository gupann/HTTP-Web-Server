#include <gtest/gtest.h>
#include "mock_file_system.h"

using namespace wasd::http;
using ::testing::Test;

class MockFileSystemTest : public Test {
protected:
  MockFileSystem fs;

  // Helper to check if a vector contains a specific item
  bool contains(const std::vector<std::string> &vec, const std::string &item) {
    return std::find(vec.begin(), vec.end(), item) != vec.end();
  }
};

// Test file_exists method
TEST_F(MockFileSystemTest, FileExists) {
  // Initially, nothing exists
  EXPECT_FALSE(fs.file_exists("/test.txt"));
  EXPECT_FALSE(fs.file_exists("/dir"));

  // Create a file and check if it exists
  fs.write_file("/test.txt", "content");
  EXPECT_TRUE(fs.file_exists("/test.txt"));

  // Create a directory and check if it exists
  fs.create_directory("/dir");
  EXPECT_TRUE(fs.file_exists("/dir"));
}

// Test read_file method
TEST_F(MockFileSystemTest, ReadFile) {
  // Reading non-existent file returns nullopt
  EXPECT_FALSE(fs.read_file("/test.txt").has_value());

  // Write and then read file
  fs.write_file("/test.txt", "Hello, World!");
  auto content = fs.read_file("/test.txt");
  EXPECT_TRUE(content.has_value());
  EXPECT_EQ(*content, "Hello, World!");

  // Try reading a directory (should return nullopt)
  fs.create_directory("/dir");
  EXPECT_FALSE(fs.read_file("/dir").has_value());
}

// Test write_file method
TEST_F(MockFileSystemTest, WriteFile) {
  // Write a file
  EXPECT_TRUE(fs.write_file("/test.txt", "content1"));
  EXPECT_EQ(*fs.read_file("/test.txt"), "content1");

  // Overwrite existing file
  EXPECT_TRUE(fs.write_file("/test.txt", "content2"));
  EXPECT_EQ(*fs.read_file("/test.txt"), "content2");

  // Write file in a non-existent directory
  EXPECT_TRUE(fs.write_file("/dir/test.txt", "content3"));
  EXPECT_TRUE(fs.file_exists("/dir")); // Directory should be created
  EXPECT_EQ(*fs.read_file("/dir/test.txt"), "content3");

  // Check that directory listing was updated
  auto files = fs.list_directory("/dir");
  EXPECT_EQ(files.size(), 1);
  EXPECT_TRUE(contains(files, "test.txt"));
}

// Test delete_file method
TEST_F(MockFileSystemTest, DeleteFile) {
  // Delete non-existent file returns false
  EXPECT_FALSE(fs.delete_file("/test.txt"));

  // Write and then delete a file
  fs.write_file("/test.txt", "content");
  EXPECT_TRUE(fs.file_exists("/test.txt"));
  EXPECT_TRUE(fs.delete_file("/test.txt"));
  EXPECT_FALSE(fs.file_exists("/test.txt"));

  // Write and delete file in a directory
  fs.write_file("/dir/test.txt", "content");
  EXPECT_TRUE(fs.file_exists("/dir/test.txt"));
  EXPECT_TRUE(fs.delete_file("/dir/test.txt"));
  EXPECT_FALSE(fs.file_exists("/dir/test.txt"));

  // Check directory listing was updated
  auto files = fs.list_directory("/dir");
  EXPECT_EQ(files.size(), 0);
  EXPECT_FALSE(contains(files, "test.txt"));
}

// Test create_directory method
TEST_F(MockFileSystemTest, CreateDirectory) {
  // Create a directory
  EXPECT_TRUE(fs.create_directory("/dir"));
  EXPECT_TRUE(fs.file_exists("/dir"));

  // Create nested directories
  EXPECT_TRUE(fs.create_directory("/dir/subdir"));
  EXPECT_TRUE(fs.file_exists("/dir/subdir"));

  // Creating an existing directory should still return true
  EXPECT_TRUE(fs.create_directory("/dir"));
}

// Test list_directory method
TEST_F(MockFileSystemTest, ListDirectory) {
  // List empty directory
  fs.create_directory("/dir");
  auto files = fs.list_directory("/dir");
  EXPECT_EQ(files.size(), 0);

  // Add files and check listing
  fs.write_file("/dir/file1.txt", "content1");
  fs.write_file("/dir/file2.txt", "content2");
  fs.write_file("/dir/file3.txt", "content3");

  files = fs.list_directory("/dir");
  EXPECT_EQ(files.size(), 3);
  EXPECT_TRUE(contains(files, "file1.txt"));
  EXPECT_TRUE(contains(files, "file2.txt"));
  EXPECT_TRUE(contains(files, "file3.txt"));

  // Delete a file and check listing
  fs.delete_file("/dir/file2.txt");
  files = fs.list_directory("/dir");
  EXPECT_EQ(files.size(), 2);
  EXPECT_TRUE(contains(files, "file1.txt"));
  EXPECT_FALSE(contains(files, "file2.txt"));
  EXPECT_TRUE(contains(files, "file3.txt"));

  // List non-existent directory
  files = fs.list_directory("/nonexistent");
  EXPECT_EQ(files.size(), 0);
}

// Test complex scenario with multiple operations
TEST_F(MockFileSystemTest, ComplexScenario) {
  // Create directories and files
  fs.create_directory("/data");
  fs.create_directory("/data/users");
  fs.create_directory("/data/products");

  // Add user files
  fs.write_file("/data/users/1", "{\"name\":\"John\",\"age\":30}");
  fs.write_file("/data/users/2", "{\"name\":\"Jane\",\"age\":25}");

  // Add product files
  fs.write_file("/data/products/101", "{\"name\":\"Laptop\",\"price\":999.99}");
  fs.write_file("/data/products/102", "{\"name\":\"Phone\",\"price\":599.99}");

  // Check directory listings
  auto users = fs.list_directory("/data/users");
  auto products = fs.list_directory("/data/products");

  EXPECT_EQ(users.size(), 2);
  EXPECT_TRUE(contains(users, "1"));
  EXPECT_TRUE(contains(users, "2"));

  EXPECT_EQ(products.size(), 2);
  EXPECT_TRUE(contains(products, "101"));
  EXPECT_TRUE(contains(products, "102"));

  // Update a file
  fs.write_file("/data/users/2", "{\"name\":\"Jane\",\"age\":26}");
  EXPECT_EQ(*fs.read_file("/data/users/2"), "{\"name\":\"Jane\",\"age\":26}");

  // Delete files
  fs.delete_file("/data/products/101");
  products = fs.list_directory("/data/products");
  EXPECT_EQ(products.size(), 1);
  EXPECT_TRUE(contains(products, "102"));

  // Check that other directories are unaffected
  users = fs.list_directory("/data/users");
  EXPECT_EQ(users.size(), 2);
}