#include <boost/beast/http.hpp>
#include <boost/json.hpp>
#include <gtest/gtest.h>

#include "crud_handler.h"
#include "mock_file_system.h"

using namespace wasd::http;
namespace http = boost::beast::http;
namespace json = boost::json;

class CrudHandlerTest : public ::testing::Test {
protected:
  CrudHandlerTest()
      : mock_fs_(std::make_shared<MockFileSystem>()), handler_("/crud", "./test_data", mock_fs_) {}

  // Convenience builder for HTTP requests
  http::request<http::string_body>
  make_request(http::verb verb, const std::string &target, const std::string &body = "",
               const std::string &content_type = "application/json") {
    http::request<http::string_body> req{verb, target, 11};
    req.set(http::field::host, "test");
    if (!body.empty()) {
      req.body() = body;
      req.prepare_payload(); // sets Content-Length
      if (!content_type.empty()) {
        req.set(http::field::content_type, content_type);
      }
    }
    return req;
  }

  std::shared_ptr<MockFileSystem> mock_fs_;
  CrudRequestHandler handler_;
};

// Test complete CRUD workflow: Create, Read, List, Update, Delete
TEST_F(CrudHandlerTest, CompleteWorkflow) {
  // 1. POST create
  auto req1 = make_request(http::verb::post, "/crud/Shoes", R"({"brand":"Nike","size":10})");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::created);
  EXPECT_TRUE(res1->find(http::field::location) != res1->end());

  // Parse response to get the created ID
  auto body1 = json::parse(res1->body()).as_object();
  EXPECT_TRUE(body1.contains("id"));
  int created_id = body1["id"].as_int64();
  EXPECT_EQ(created_id, 1);

  // 2. GET single entity
  auto req2 = make_request(http::verb::get, "/crud/Shoes/1", "");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::ok);
  EXPECT_EQ(res2->body(), R"({"brand":"Nike","size":10})");

  // 3. GET list entities
  auto req3 = make_request(http::verb::get, "/crud/Shoes", "");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::ok);
  EXPECT_EQ(res3->body(), R"(["1"])");

  // 4. PUT update existing entity
  auto req4 =
      make_request(http::verb::put, "/crud/Shoes/1", R"({"brand":"Nike","size":11,"color":"red"})");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::no_content);

  // 5. GET updated entity to verify change
  auto req5 = make_request(http::verb::get, "/crud/Shoes/1", "");
  auto res5 = handler_.handle_request(req5);
  EXPECT_EQ(res5->result(), http::status::ok);
  EXPECT_EQ(res5->body(), R"({"brand":"Nike","size":11,"color":"red"})");

  // 6. DELETE entity
  auto req6 = make_request(http::verb::delete_, "/crud/Shoes/1", "");
  auto res6 = handler_.handle_request(req6);
  EXPECT_EQ(res6->result(), http::status::no_content);

  // 7. GET after delete should return 404
  auto req7 = make_request(http::verb::get, "/crud/Shoes/1", "");
  auto res7 = handler_.handle_request(req7);
  EXPECT_EQ(res7->result(), http::status::not_found);
}

// Test POST validation and error cases
TEST_F(CrudHandlerTest, PostValidationAndErrors) {
  // Test invalid JSON
  auto req1 = make_request(http::verb::post, "/crud/Shoes", "{invalid json");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::bad_request);
  EXPECT_TRUE(res1->body().find("Invalid JSON") != std::string::npos);

  // Test unsupported content type
  auto req2 = make_request(http::verb::post, "/crud/Shoes", R"({"brand":"Nike"})", "text/plain");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::unsupported_media_type);

  // Test empty body
  auto req3 = make_request(http::verb::post, "/crud/Shoes", "");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::bad_request);

  // Test missing content-type (should still work)
  auto req4 = make_request(http::verb::post, "/crud/Shoes", R"({"brand":"Nike"})", "");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::created);
}

// Test PUT validation and upsert behavior
TEST_F(CrudHandlerTest, PutValidationAndUpsert) {
  // Test PUT creating new entity (upsert)
  auto req1 = make_request(http::verb::put, "/crud/Shoes/42", R"({"brand":"Adidas","size":9})");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::created);
  EXPECT_TRUE(res1->find(http::field::location) != res1->end());

  // Verify the entity was created
  auto req2 = make_request(http::verb::get, "/crud/Shoes/42", "");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::ok);
  EXPECT_EQ(res2->body(), R"({"brand":"Adidas","size":9})");

  // Test PUT updating existing entity
  auto req3 = make_request(http::verb::put, "/crud/Shoes/42", R"({"brand":"Adidas","size":10})");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::no_content);

  // Test PUT with invalid JSON
  auto req4 = make_request(http::verb::put, "/crud/Shoes/1", "{invalid json");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::bad_request);

  // Test PUT without ID
  auto req5 = make_request(http::verb::put, "/crud/Shoes", R"({"brand":"Nike"})");
  auto res5 = handler_.handle_request(req5);
  EXPECT_EQ(res5->result(), http::status::bad_request);
  EXPECT_TRUE(res5->body().find("PUT requests require an ID") != std::string::npos);
}

// Test DELETE operations and edge cases
TEST_F(CrudHandlerTest, DeleteOperations) {
  // Create an entity first
  auto req1 = make_request(http::verb::post, "/crud/Items", R"({"name":"test"})");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::created);

  // Delete the entity
  auto req2 = make_request(http::verb::delete_, "/crud/Items/1", "");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::no_content);

  // Test DELETE without ID
  auto req3 = make_request(http::verb::delete_, "/crud/Items", "");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::bad_request);
  EXPECT_TRUE(res3->body().find("DELETE requests require an ID") != std::string::npos);

  // Test DELETE non-existent entity
  auto req4 = make_request(http::verb::delete_, "/crud/Items/999", "");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::not_found);
}

// Test HTTP method validation
TEST_F(CrudHandlerTest, HttpMethodValidation) {
  // Test unsupported HTTP method
  auto req1 = make_request(http::verb::patch, "/crud/Shoes/1", "");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::method_not_allowed);

  // Test OPTIONS method (not supported)
  auto req2 = make_request(http::verb::options, "/crud/Shoes", "");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::method_not_allowed);
}

// Test path validation and parsing
TEST_F(CrudHandlerTest, PathValidationAndParsing) {
  // Test invalid path (not starting with prefix)
  auto req1 = make_request(http::verb::get, "/invalid/path", "");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::not_found);

  // Test empty entity type
  auto req2 = make_request(http::verb::get, "/crud/", "");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::bad_request);

  // Test path with too many segments
  auto req3 = make_request(http::verb::get, "/crud/Shoes/1/extra/segment", "");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::bad_request);

  // Test path with empty segments
  auto req4 = make_request(http::verb::get, "/crud//1", "");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::bad_request);
}

// Test multiple entities and ID generation
TEST_F(CrudHandlerTest, MultipleEntitiesAndIdGeneration) {
  // Create multiple entities of same type
  auto req1 = make_request(http::verb::post, "/crud/Books", R"({"title":"Book1"})");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::created);

  auto req2 = make_request(http::verb::post, "/crud/Books", R"({"title":"Book2"})");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::created);

  auto req3 = make_request(http::verb::post, "/crud/Books", R"({"title":"Book3"})");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::created);

  // List all entities
  auto req_list = make_request(http::verb::get, "/crud/Books", "");
  auto res_list = handler_.handle_request(req_list);
  EXPECT_EQ(res_list->result(), http::status::ok);

  // Should contain all three IDs
  std::string body = res_list->body();
  EXPECT_TRUE(body.find("\"1\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"2\"") != std::string::npos);
  EXPECT_TRUE(body.find("\"3\"") != std::string::npos);

  // Create entities of different type
  auto req4 = make_request(http::verb::post, "/crud/Movies", R"({"title":"Movie1"})");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::created);

  // Should start with ID 1 for new entity type
  auto body4 = json::parse(res4->body()).as_object();
  EXPECT_EQ(body4["id"].as_int64(), 1);
}

// Test ID generation with existing files
TEST_F(CrudHandlerTest, IdGenerationWithExistingFiles) {
  // Manually create some files to simulate existing entities
  mock_fs_->write_file("./test_data/Cars/5", R"({"model":"Tesla"})");
  mock_fs_->write_file("./test_data/Cars/10", R"({"model":"BMW"})");
  mock_fs_->write_file("./test_data/Cars/2", R"({"model":"Ford"})");

  // Create a new entity - should get ID 11 (max + 1)
  auto req = make_request(http::verb::post, "/crud/Cars", R"({"model":"Honda"})");
  auto res = handler_.handle_request(req);
  EXPECT_EQ(res->result(), http::status::created);

  auto body = json::parse(res->body()).as_object();
  EXPECT_EQ(body["id"].as_int64(), 11);
}

// Test file system error handling
TEST_F(CrudHandlerTest, FileSystemErrorHandling) {
  // Test write failure
  mock_fs_->set_write_should_fail(true);
  auto req1 = make_request(http::verb::post, "/crud/Shoes", R"({"brand":"Nike"})");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::internal_server_error);
  EXPECT_TRUE(res1->body().find("Failed to save entity") != std::string::npos);

  // Reset and test read failure
  mock_fs_->set_write_should_fail(false);
  mock_fs_->write_file("./test_data/Items/1", R"({"name":"test"})");
  mock_fs_->set_read_should_fail(true);

  auto req2 = make_request(http::verb::get, "/crud/Items/1", "");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::internal_server_error);

  // Reset and test delete failure
  mock_fs_->set_read_should_fail(false);
  mock_fs_->set_delete_should_fail(true);

  auto req3 = make_request(http::verb::delete_, "/crud/Items/1", "");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::internal_server_error);
}

// Test content type edge cases
TEST_F(CrudHandlerTest, ContentTypeEdgeCases) {
  // Test with no content-type header (should work)
  auto req1 = make_request(http::verb::post, "/crud/Test", R"({"data":"test"})", "");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::created);

  // Test with application/json; charset=utf-8 (should fail - exact match required)
  auto req2 = make_request(http::verb::post, "/crud/Test", R"({"data":"test"})",
                           "application/json; charset=utf-8");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::unsupported_media_type);
}

// Test JSON edge cases
TEST_F(CrudHandlerTest, JsonEdgeCases) {
  // Test with empty JSON object
  auto req1 = make_request(http::verb::post, "/crud/Empty", "{}");
  auto res1 = handler_.handle_request(req1);
  EXPECT_EQ(res1->result(), http::status::created);

  // Test with complex nested JSON
  auto req2 = make_request(
      http::verb::post, "/crud/Complex",
      R"({"user":{"name":"John","details":{"age":30,"hobbies":["reading","coding"]}}})");
  auto res2 = handler_.handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::created);

  // Test with JSON array (should work)
  auto req3 = make_request(http::verb::post, "/crud/Array", R"([1,2,3])");
  auto res3 = handler_.handle_request(req3);
  EXPECT_EQ(res3->result(), http::status::created);

  // Test with JSON string (should work)
  auto req4 = make_request(http::verb::post, "/crud/String", R"("just a string")");
  auto res4 = handler_.handle_request(req4);
  EXPECT_EQ(res4->result(), http::status::created);
}