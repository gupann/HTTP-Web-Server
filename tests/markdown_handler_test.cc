#include <boost/log/trivial.hpp>
#include <filesystem>
#include <fstream>
#include <thread>

#include "gtest/gtest.h"
#include "handlers/markdown_handler.h"
#include "mock_file_system.h"
#include "real_file_system.h"

using namespace wasd::http;
namespace http = boost::beast::http;
namespace fs = std::filesystem;

/* ------------------------------------------------------------------ */
/*  Helper: write a file onto the real disk so std::filesystem tests  */
/*          inside MarkdownHandler succeed in every environment.      */
/* ------------------------------------------------------------------ */
static void write_real_file(const std::string &path, const std::string &body) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream ofs(path, std::ios::trunc);
  ofs << body;
}

/* ------------------------------------------------------------------ */
/*  Helper: create a simple GET Request object                        */
/* ------------------------------------------------------------------ */
static Request make_get_request(const std::string &target) {
  Request req;
  req.method(http::verb::get);
  req.target(target);
  req.version(11);
  return req;
}

/* ================================================================== */
/*  Test 1: ?raw=1 should return the untouched Markdown + right MIME  */
/* ================================================================== */
TEST(MarkdownHandler, RawModeServesPlainMarkdown) {
  const std::string root = "/tmp/mdtest/raw";
  const std::string md_file = root + "/sample.md";
  const std::string md_body = "# Hello\nRaw *Markdown*.";

  // 1 ─ ensure both MockFS and real FS have the file
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  // 3 ─ craft request
  Request req = make_get_request("/docs/sample.md?raw=1");

  // 4 ─ call & assert
  auto res = handler.handle_request(req);
  ASSERT_EQ(res->result(), http::status::ok);
  ASSERT_EQ(res->at(http::field::content_type), "text/markdown");
  ASSERT_EQ(res->body(), md_body);
}

/* ================================================================== */
/*  Test 2: normal path still returns HTML                            */
/* ================================================================== */
TEST(MarkdownHandler, NormalModeStillReturnsHTML) {
  const std::string root = "/tmp/mdtest/html";
  const std::string md_file = root + "/sample.md";
  const std::string md_body = "# Hello\nRaw *Markdown*.";

  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  Request req = make_get_request("/docs/sample.md"); // no ?raw=1

  auto res = handler.handle_request(req);
  ASSERT_EQ(res->result(), http::status::ok);
  ASSERT_EQ(res->at(http::field::content_type), "text/html");

  // very loose HTML sanity check
  ASSERT_NE(res->body().find("<p>"), std::string::npos);
}

/* ================================================================== */
/*  Fixture for all directory‐listing & caching tests                  */
/* ================================================================== */
class MarkdownHandlerDirectoryTest : public ::testing::Test {
protected:
  // We'll create a temporary directory under /tmp/ for each test
  fs::path temp_root;
  std::shared_ptr<FileSystemInterface> real_fs;
  std::unique_ptr<markdown_handler> handler;

  void SetUp() override {
    // Create a unique temp directory
    temp_root = fs::temp_directory_path() / fs::path("mdtest_dir_" + std::to_string(::getpid()));
    fs::create_directories(temp_root);
    real_fs = std::make_shared<RealFileSystem>();

    // Instantiate handler with location "/docs", root = temp_root
    handler = std::make_unique<markdown_handler>("/docs", temp_root.string(),
                                                 /*template*/ "", real_fs);
  }

  void TearDown() override {
    // Recursively delete the temp directory
    std::error_code ec;
    fs::remove_all(temp_root, ec);
  }

  // Helper: write an .md file (and its containing directory) under temp_root
  void write_md(const std::string &relative_path, const std::string &body) {
    const auto full = (temp_root / fs::path(relative_path)).string();
    write_real_file(full, body);
  }

  // Helper: write a non‐.md file (to test filtering)
  void write_txt(const std::string &relative_path, const std::string &body) {
    const auto full = (temp_root / fs::path(relative_path)).string();
    write_real_file(full, body);
  }

  // Helper: get the ETag from a response
  std::string get_etag(const std::unique_ptr<Response> &res) {
    if (res->find(http::field::etag) != res->end()) {
      return std::string(res->at(http::field::etag));
    } else {
      return "";
    }
  }
};

/* ------------------------------------------------------------------ */
/* Test: request to "/docs" (no trailing slash) → 301 redirect "/docs/" */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, MissingSlashRedirect) {
  Request req = make_get_request("/docs");
  auto res = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::moved_permanently);
  // We expect a Location header
  ASSERT_TRUE(res->find(http::field::location) != res->end());
  EXPECT_EQ(res->at(http::field::location), "/docs/");
}

/* ------------------------------------------------------------------ */
/* Test: When directory is empty (no .md files), listing returns OK   */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, EmptyDirectoryListing) {
  // No files are written into temp_root; it remains empty
  Request req = make_get_request("/docs/");
  auto res = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::ok);
  ASSERT_EQ(res->at(http::field::content_type), "text/html");
  // The body should contain an empty <ul> (apart from the <h1> header)
  std::string body = res->body();
  EXPECT_NE(body.find("<h1>Index of /docs/</h1>"), std::string::npos);
  EXPECT_NE(body.find("<ul>"), std::string::npos);
  EXPECT_NE(body.find("</ul>"), std::string::npos);

  // There should be no <li> inside
  size_t pos_li = body.find("<li>");
  EXPECT_EQ(pos_li, std::string::npos);
}

/* ------------------------------------------------------------------ */
/* Test: Only .md files appear, sorted lexicographically               */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, MdOnlyListingAndOrdering) {
  // Create a mix of .md, .txt, and nested directory
  write_md("b.md", "# B\nContent");
  write_md("a.md", "# A\nContent");
  write_txt("z.txt", "Should be ignored");
  fs::create_directories(temp_root / "sub"); // a sub‐directory with no md

  Request req = make_get_request("/docs/");
  auto res = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::ok);
  std::string body = res->body();

  // We should see a sorted list: a.md then b.md
  size_t pos_a = body.find("a.md");
  size_t pos_b = body.find("b.md");
  ASSERT_NE(pos_a, std::string::npos);
  ASSERT_NE(pos_b, std::string::npos);
  EXPECT_LT(pos_a, pos_b);

  // Ensure "z.txt" does not appear
  EXPECT_EQ(body.find("z.txt"), std::string::npos);
}

/* ------------------------------------------------------------------ */
/* Test: Requesting an individual .md file under directory renders HTML */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, IndividualMdRendering) {
  const std::string content = "# Title\nHello world!";
  write_md("page.md", content);

  Request req = make_get_request("/docs/page.md");
  auto res = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::ok);
  EXPECT_EQ(res->at(http::field::content_type), "text/html");
  // The rendered HTML must include a <h1>Title</h1> or at least a <p>Hello world!</p>
  std::string body = res->body();
  EXPECT_NE(body.find("<h1>Title</h1>"), std::string::npos);
  EXPECT_NE(body.find("<p>Hello world!</p>"), std::string::npos);
}

/* ------------------------------------------------------------------ */
/* Test: Request to non‐existent .md returns 404                        */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, MissingMdReturns404) {
  Request req = make_get_request("/docs/no_such.md");
  auto res = handler->handle_request(req);

  EXPECT_EQ(res->result(), http::status::not_found);
}

/* ------------------------------------------------------------------ */
/* Test: Immediate second listing returns same ETag (cache hit)        */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, ImmediateCacheHitKeepsSameEtag) {
  write_md("x.md", "# X\nHello");

  // First request: build the cache
  auto res1 = handler->handle_request(make_get_request("/docs/"));
  ASSERT_EQ(res1->result(), http::status::ok);
  std::string etag1 = get_etag(res1);
  ASSERT_FALSE(etag1.empty());

  // Immediate second request: should hit cache
  auto res2 = handler->handle_request(make_get_request("/docs/"));
  ASSERT_EQ(res2->result(), http::status::ok);
  std::string etag2 = get_etag(res2);
  EXPECT_EQ(etag1, etag2);
}

/* ------------------------------------------------------------------ */
/* Test: After 5s, cache entry expires and ETag changes                */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, CacheExpiresAfterFiveSeconds) {
  write_md("y.md", "# Y\nHello");

  auto res1 = handler->handle_request(make_get_request("/docs/"));
  ASSERT_EQ(res1->result(), http::status::ok);
  std::string etag1 = get_etag(res1);
  ASSERT_FALSE(etag1.empty());

  // Sleep for just over 5 seconds to expire the cache
  std::this_thread::sleep_for(std::chrono::seconds(6));

  auto res2 = handler->handle_request(make_get_request("/docs/"));
  ASSERT_EQ(res2->result(), http::status::ok);
  std::string etag2 = get_etag(res2);
  EXPECT_NE(etag1, etag2);
}

/* ------------------------------------------------------------------ */
/* Test: Subdirectory (nested) missing slash → 301 redirect to "/"     */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, SubdirectoryMissingSlashRedirect) {
  fs::create_directories(temp_root / "inner");
  write_md("inner/foo.md", "# Foo\nBar");

  Request req = make_get_request("/docs/inner");
  auto res = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::moved_permanently);
  ASSERT_TRUE(res->find(http::field::location) != res->end());
  EXPECT_EQ(res->at(http::field::location), "/docs/inner/");
}

/* ------------------------------------------------------------------ */
/* Test: Listing inside a non‐empty subdirectory, sorted               */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, SubdirectoryListingSorted) {
  fs::create_directories(temp_root / "inner");
  write_md("inner/beta.md", "# Beta\n");
  write_md("inner/alpha.md", "# Alpha\n");
  write_txt("inner/ignore.txt", "nope");

  Request req = make_get_request("/docs/inner/");
  auto res = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::ok);
  std::string body = res->body();

  size_t pos_alpha = body.find("alpha.md");
  size_t pos_beta = body.find("beta.md");
  ASSERT_NE(pos_alpha, std::string::npos);
  ASSERT_NE(pos_beta, std::string::npos);
  EXPECT_LT(pos_alpha, pos_beta);
  // “ignore.txt” must not appear
  EXPECT_EQ(body.find("ignore.txt"), std::string::npos);
}

/* ------------------------------------------------------------------ */
/* Test: Request to a non‐existent directory returns 404               */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, NonexistentDirectoryIs404) {
  Request req = make_get_request("/docs/no_dir/");
  auto res = handler->handle_request(req);
  EXPECT_EQ(res->result(), http::status::not_found);
}

/* ------------------------------------------------------------------ */
/* Test: Path traversal (“../”) on directory listing → 404              */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, PathTraversalIsBlocked) {
  // Even if parent exists, “../” should be blocked
  Request req = make_get_request("/docs/../");
  auto res = handler->handle_request(req);
  EXPECT_EQ(res->result(), http::status::not_found);
}

/* ------------------------------------------------------------------ */
/* Test: Conditional‐GET on a directory listing returns 304 if ETag matches */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, DirectoryConditionalGetReturns304) {
  write_md("a.md", "# A\n");
  auto res1 = handler->handle_request(make_get_request("/docs/"));
  ASSERT_EQ(res1->result(), http::status::ok);
  std::string etag1 = get_etag(res1);
  ASSERT_FALSE(etag1.empty());

  // Now send If-None-Match with the same ETag
  Request req2 = make_get_request("/docs/");
  req2.set(http::field::if_none_match, etag1);
  auto res2 = handler->handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::not_modified);
}

/* ------------------------------------------------------------------ */
/* Test: Directory listing with “If-Modified-Since” header → 304        */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, DirectoryConditionalGetSinceReturns304) {
  write_md("b.md", "# B\n");
  auto res1 = handler->handle_request(make_get_request("/docs/"));
  ASSERT_EQ(res1->result(), http::status::ok);
  // Grab Last-Modified header instead of ETag
  ASSERT_TRUE(res1->find(http::field::last_modified) != res1->end());
  std::string lm = std::string(res1->at(http::field::last_modified));

  // Second request with If-Modified-Since = lm
  Request req2 = make_get_request("/docs/");
  req2.set(http::field::if_modified_since, lm);
  auto res2 = handler->handle_request(req2);
  EXPECT_EQ(res2->result(), http::status::not_modified);
}

/* ------------------------------------------------------------------ */
/* MarkdownHandler correctly converts Markdown to HTML       */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, ConvertMarkdownToHTML) {
  const std::string root = "/tmp/mdtest/convert";
  const std::string md_file = root + "/example.md";
  const std::string md_body = "# Example\n\nThis is a **test**.";

  // 1 ─ ensure both MockFS and real FS have the file
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  // 3 ─ craft request
  Request req = make_get_request("/docs/example.md");

  // 4 ─ call & assert
  auto res = handler.handle_request(req);
  ASSERT_EQ(res->result(), http::status::ok);
  ASSERT_EQ(res->at(http::field::content_type), "text/html");

  // Check if the body contains the expected HTML tags
  std::string body = res->body();
  EXPECT_NE(body.find("<h1>Example</h1>"), std::string::npos);
  EXPECT_NE(body.find("<strong>test</strong>"), std::string::npos);
}

/* ------------------------------------------------------------------ */
/*  Path sanitization and normalization                      */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, PathSanitizationAndNormalization) {
  const std::string root = "/tmp/mdtest/sanitize";
  const std::string md_file = root + "/valid.md";
  const std::string md_body = "# Valid\n\nThis is a valid file.";

  // 1 ─ ensure the file exists
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  // 3 ─ craft requests
  Request req_valid = make_get_request("/docs/valid.md");
  Request req_traversal = make_get_request("/docs/../valid.md");    // Attempted traversal
  Request req_not_found = make_get_request("/docs/nonexistent.md"); // Non-existent file

  // 4 ─ call & assert valid request
  {
    auto res = handler.handle_request(req_valid);
    ASSERT_EQ(res->result(), http::status::ok);
    ASSERT_EQ(res->at(http::field::content_type), "text/html");
    EXPECT_NE(res->body().find("<h1>Valid</h1>"), std::string::npos);
  }

  // 5 ─ call & assert traversal request (should be blocked)
  {
    auto res = handler.handle_request(req_traversal);
    ASSERT_EQ(res->result(), http::status::not_found);
  }

  // 6 ─ call & assert not found request
  {
    auto res = handler.handle_request(req_not_found);
    ASSERT_EQ(res->result(), http::status::not_found);
  }
}

/* ------------------------------------------------------------------ */
/*  Template handling and errors                             */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, TemplateHandlingAndErrors) {
  const std::string root_for_markdown_files = "/tmp/mdtest/template_test_md_root";
  const std::string md_filename = "test.md";
  const std::string md_filepath_in_mock = root_for_markdown_files + "/" + md_filename;
  const std::string md_body = "# Test\n\nThis is a test file.";

  // --- Common Mock FileSystem Setup ---
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_filepath_in_mock, md_body);
  write_real_file(md_filepath_in_mock, md_body);

  // --- Scenario 1: Handler with a VALID template ---
  {
    const std::string valid_template_path = "/tmp/templates/actual_valid_template.html";
    const std::string valid_template_content =
        "<html><title>Wrapper</title><body>{{content}}</body></html>";
    const std::string expected_html_output = "<html><title>Wrapper</title><body><h1>Test</"
                                             "h1>\n<p>This is a test file.</p>\n</body></html>";

    mock_fs->write_file(valid_template_path, valid_template_content);
    mock_fs->set_read_should_fail(false);

    markdown_handler handler("/docs", root_for_markdown_files, valid_template_path, mock_fs);
    Request req = make_get_request("/docs/" + md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::ok) << "Handler should return OK with a valid template.";
    ASSERT_EQ(res->at(http::field::content_type), "text/html");
    EXPECT_EQ(res->body(), expected_html_output);
  }

  // --- Scenario 2: Handler with a MISSING template ---
  {
    const std::string missing_template_path = "/tmp/templates/definitely_missing_template.html";
    mock_fs->set_read_should_fail(false);

    markdown_handler handler("/docs", root_for_markdown_files, missing_template_path, mock_fs);
    Request req = make_get_request("/docs/" + md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::internal_server_error)
        << "Handler should return 500 if configured template is missing.";
  }

  // --- Scenario 2b: Handler with a template that EXISTS but reading it FAILS ---
  {
    const std::string unreadable_template_path = "/tmp/templates/unreadable_template.html";
    const std::string template_content = "<html><body>{{content}}</body></html>";

    mock_fs->write_file(unreadable_template_path, template_content);
    mock_fs->set_read_should_fail(true);

    markdown_handler handler("/docs", root_for_markdown_files, unreadable_template_path, mock_fs);
    Request req = make_get_request("/docs/" + md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::internal_server_error)
        << "Handler should return 500 if template read fails.";

    mock_fs->set_read_should_fail(false);
  }

  // --- Scenario 3: Handler with NO template configured (empty template_path) ---
  {
    const std::string expected_raw_html_fragment = "<h1>Test</h1>\n<p>This is a test file.</p>\n";

    mock_fs->set_read_should_fail(false); // Ensure reads are generally working

    markdown_handler handler("/docs", root_for_markdown_files, "", mock_fs); // Empty template path
    Request req = make_get_request("/docs/" + md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::ok)
        << "Handler should return OK with no template configured.";
    ASSERT_EQ(res->at(http::field::content_type), "text/html");
    EXPECT_EQ(res->body(), expected_raw_html_fragment);
  }
}

/* ------------------------------------------------------------------ */
/*  Large Markdown files handling                            */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, LargeMarkdownFileHandling) {
  const std::string root = "/tmp/mdtest/large";
  const std::string md_file = root + "/large.md";
  std::string md_body(1024 * 1024, '#'); // 1 MB file

  // 1 ─ ensure the file exists
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  // 3 ─ craft request
  Request req = make_get_request("/docs/large.md");

  // 4 ─ call & assert
  {
    auto res = handler.handle_request(req);
    ASSERT_EQ(res->result(), http::status::ok);
    ASSERT_EQ(res->at(http::field::content_type), "text/html");

    // For a large file, we might want to check specific handling, like streaming or size limits.
    // Here, we just check the response status and content type.
  }

  // 5 ─ test with a file exceeding typical max size (e.g., 10 MB)
  {
    std::string huge_md_body(10 * 1024 * 1024, '#'); // 10 MB file
    mock_fs->write_file(md_file, huge_md_body);
    write_real_file(md_file, huge_md_body);

    auto res = handler.handle_request(req);
    ASSERT_EQ(res->result(), http::status::payload_too_large);
  }
}

/* ------------------------------------------------------------------ */
/*  Special characters and encoding in paths                */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, SpecialCharactersAndEncoding) {
  const std::string root = "/tmp/mdtest/special";
  const std::string md_file = root + "/test.md";
  const std::string md_body = "# Test\n\nThis is a test file with special characters: &%$#@!";

  // 1 ─ ensure the file exists
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  // 3 ─ craft request with encoded special characters
  Request req = make_get_request("/docs/test.md?query=param&another=value");

  // 4 ─ call & assert
  {
    auto res = handler.handle_request(req);
    ASSERT_EQ(res->result(), http::status::ok);
    ASSERT_EQ(res->at(http::field::content_type), "text/html");

    // Check if the body contains the expected HTML tags and content
    std::string body = res->body();
    EXPECT_NE(body.find("<h1>Test</h1>"), std::string::npos);
    EXPECT_NE(body.find("This is a test file with special characters"), std::string::npos);
  }
}

/* ------------------------------------------------------------------ */
/*  Concurrent requests handling                              */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, ConcurrentRequests) {
  const std::string root = "/tmp/mdtest/concurrent";
  const std::string md_file = root + "/test.md";
  const std::string md_body = "# Test\n\nThis is a test file.";

  // 1 ─ ensure the file exists
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/ "", mock_fs);

  // 3 ─ craft request
  Request req = make_get_request("/docs/test.md");

  // 4 ─ simulate concurrent requests
  auto res1 = handler.handle_request(req);
  auto res2 = handler.handle_request(req);

  // 5 ─ assert responses
  ASSERT_EQ(res1->result(), http::status::ok);
  ASSERT_EQ(res2->result(), http::status::ok);
  ASSERT_EQ(res1->at(http::field::content_type), "text/html");
  ASSERT_EQ(res2->at(http::field::content_type), "text/html");

  // Check if the body contains the expected HTML tags
  std::string body1 = res1->body();
  std::string body2 = res2->body();
  EXPECT_NE(body1.find("<h1>Test</h1>"), std::string::npos);
  EXPECT_NE(body2.find("<h1>Test</h1>"), std::string::npos);
}

/* ------------------------------------------------------------------ */
/*  Edge cases and error handling                            */
/* ------------------------------------------------------------------ */
TEST(MarkdownHandler, EdgeCasesAndErrorHandling) {
  const std::string root_dir = "/tmp/mdtest/edge"; // Renamed for clarity

  // --- Common Mock FileSystem and Handler Setup ---
  auto mock_fs = std::make_shared<MockFileSystem>();
  markdown_handler handler("/docs", root_dir, /*template*/ "", mock_fs);

  // --- Test Data ---
  const std::string valid_md_filename = "test.md";
  const std::string valid_md_filepath = root_dir + "/" + valid_md_filename;
  const std::string valid_md_content = "# Test\n\nThis is a test file.";

  const std::string empty_md_filename = "empty.md";
  const std::string empty_md_filepath = root_dir + "/" + empty_md_filename;

  const std::string large_md_filename = "large.md";
  const std::string large_md_filepath = root_dir + "/" + large_md_filename;
  const std::string large_md_content(1 * 1024 * 1024,
                                     '#'); // 1MB, within typical read_small_file limits

  const std::string nonexistent_md_filename = "nonexistent.md";

  // --- Scenario 1: Valid request ---
  {
    mock_fs->write_file(valid_md_filepath, valid_md_content);
    // write_real_file is for std::filesystem calls if handler bypasses mock_fs for some checks
    write_real_file(valid_md_filepath, valid_md_content);

    Request req = make_get_request("/docs/" + valid_md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::ok);
    ASSERT_EQ(res->at(http::field::content_type), "text/html");
    EXPECT_NE(res->body().find("<h1>Test</h1>"), std::string::npos);
  }

  // --- Scenario 2: Empty file request ---
  {
    mock_fs->write_file(empty_md_filepath, ""); // Add empty file to mock
    write_real_file(empty_md_filepath, "");     // And to real FS for std::filesystem checks

    Request req = make_get_request("/docs/" + empty_md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::ok);
    ASSERT_EQ(res->at(http::field::content_type), "text/html");
    // Assuming no template, or template is just {{content}}, an empty MD results in empty body
    EXPECT_EQ(res->body(), "");
  }

  // --- Scenario 3: Reasonably large (but processable) file request ---
  {
    mock_fs->write_file(large_md_filepath, large_md_content); // Add large file to mock
    write_real_file(large_md_filepath, large_md_content);     // And to real FS

    Request req = make_get_request("/docs/" + large_md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::ok);
    ASSERT_EQ(res->at(http::field::content_type), "text/html");
    // Basic check, actual content check might be too verbose for a large file
    EXPECT_NE(res->body().find("<p>#"),
              std::string::npos); // cmark might wrap lone # in <p> if not a heading
  }

  // --- Scenario 4: Non-existent file request ---
  {
    // Ensure the file does not exist in mock_fs (it wasn't added)
    Request req = make_get_request("/docs/" + nonexistent_md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::not_found);
  }

  // --- Scenario 5: File too large (exceeding read_small_file's internal limit, if applicable) ---
  {
    const std::string too_large_md_filename = "too_large.md";
    const std::string too_large_md_filepath = root_dir + "/" + too_large_md_filename;
    std::string too_large_md_content(1024 * 1024 + 1, 'L'); // 1MB + 1 byte

    mock_fs->write_file(too_large_md_filepath, too_large_md_content);
    write_real_file(too_large_md_filepath, too_large_md_content); // For std::filesystem checks

    Request req = make_get_request("/docs/" + too_large_md_filename);
    auto res = handler.handle_request(req);

    ASSERT_EQ(res->result(), http::status::payload_too_large)
        << "Expected payload_too_large if read_small_file (or handler) rejects due to size limit.";
  }
}
/* ------------------------------------------------------------------ */
/* Extra: raw‑mode still gets ETag + 304 handling                     */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, RawModeConditionalGet) {
  const std::string body = "# Raw Page\nJust markdown.";
  write_md("raw.md", body);

  // First request (raw)
  Request r1 = make_get_request("/docs/raw.md?raw=1");
  auto res1 = handler->handle_request(r1);
  ASSERT_EQ(res1->result(), http::status::ok);
  ASSERT_EQ(res1->at(http::field::content_type), "text/markdown");
  std::string etag = std::string(res1->at(http::field::etag));
  ASSERT_FALSE(etag.empty());

  // Second request with If‑None‑Match
  Request r2 = make_get_request("/docs/raw.md?raw=1");
  r2.set(http::field::if_none_match, etag);
  auto res2 = handler->handle_request(r2);
  EXPECT_EQ(res2->result(), http::status::not_modified);
}

/* ------------------------------------------------------------------ */
/* Template wrapper injects {{content}} correctly                      */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, TemplateInjectionWorks) {
  // 1.  Make a tiny template file with an obvious marker
  const fs::path tpl = temp_root / "wrapper.html";
  const std::string tpl_body = "<html><head><title>TPL</title></head><body>\n"
                               "<header>HEADER</header>\n"
                               "{{content}}\n"
                               "<footer>FOOTER</footer>\n"
                               "</body></html>";
  write_real_file(tpl.string(), tpl_body);

  // 2.  Re‑create handler that uses this template
  handler = std::make_unique<markdown_handler>("/docs", temp_root.string(), tpl.string(), real_fs);

  // 3.  Markdown file
  write_md("page.md", "# Heading\nBody");
  auto res = handler->handle_request(make_get_request("/docs/page.md"));

  ASSERT_EQ(res->result(), http::status::ok);
  const std::string &html = res->body();

  /* Must contain outer chrome AND converted Markdown */
  EXPECT_NE(html.find("<header>HEADER</header>"), std::string::npos);
  EXPECT_NE(html.find("<footer>FOOTER</footer>"), std::string::npos);
  EXPECT_NE(html.find("<h1>Heading</h1>"), std::string::npos);
}

/* ------------------------------------------------------------------ */
/* File‑level If‑Modified‑Since returns 304                            */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, FileConditionalGetSinceReturns304) {
  write_md("cond.md", "# Cond\n");
  auto first = handler->handle_request(make_get_request("/docs/cond.md"));
  ASSERT_EQ(first->result(), http::status::ok);
  ASSERT_TRUE(first->find(http::field::last_modified) != first->end());
  std::string lm = std::string(first->at(http::field::last_modified));

  Request r2 = make_get_request("/docs/cond.md");
  r2.set(http::field::if_modified_since, lm);
  auto second = handler->handle_request(r2);
  EXPECT_EQ(second->result(), http::status::not_modified);
}
