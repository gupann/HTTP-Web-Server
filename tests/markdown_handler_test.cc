#include <boost/log/trivial.hpp>
#include <filesystem>
#include <fstream>
#include <thread>

#include "gtest/gtest.h"
#include "handlers/markdown_handler.h"
#include "real_file_system.h"
#include "mock_file_system.h"

using namespace wasd::http;
namespace http = boost::beast::http;
namespace fs   = std::filesystem;

/* ------------------------------------------------------------------ */
/*  Helper: write a file onto the real disk so std::filesystem tests  */
/*          inside MarkdownHandler succeed in every environment.      */
/* ------------------------------------------------------------------ */
static void write_real_file(const std::string& path, const std::string& body) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream ofs(path, std::ios::trunc);
  ofs << body;
}

/* ------------------------------------------------------------------ */
/*  Helper: create a simple GET Request object                        */
/* ------------------------------------------------------------------ */
static Request make_get_request(const std::string& target) {
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
  const std::string root    = "/tmp/mdtest/raw";
  const std::string md_file = root + "/sample.md";
  const std::string md_body = "# Hello\nRaw *Markdown*.";

  // 1 ─ ensure both MockFS and real FS have the file
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  // 2 ─ build handler
  markdown_handler handler("/docs", root, /*template*/"", mock_fs);

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
  const std::string root    = "/tmp/mdtest/html";
  const std::string md_file = root + "/sample.md";
  const std::string md_body = "# Hello\nRaw *Markdown*.";

  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_file, md_body);
  write_real_file(md_file, md_body);

  markdown_handler handler("/docs", root, /*template*/"", mock_fs);

  Request req = make_get_request("/docs/sample.md");   // no ?raw=1

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
    handler = std::make_unique<markdown_handler>("/docs",
                                                 temp_root.string(),
                                                 /*template*/"",
                                                 real_fs);
  }

  void TearDown() override {
    // Recursively delete the temp directory
    std::error_code ec;
    fs::remove_all(temp_root, ec);
  }

  // Helper: write an .md file (and its containing directory) under temp_root
  void write_md(const std::string& relative_path, const std::string& body) {
    const auto full = (temp_root / fs::path(relative_path)).string();
    write_real_file(full, body);
  }

  // Helper: write a non‐.md file (to test filtering)
  void write_txt(const std::string& relative_path, const std::string& body) {
    const auto full = (temp_root / fs::path(relative_path)).string();
    write_real_file(full, body);
  }

  // Helper: get the ETag from a response
  std::string get_etag(const std::unique_ptr<Response>& res) {
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
  auto res    = handler->handle_request(req);

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
  auto res    = handler->handle_request(req);

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
  write_md("b.md",   "# B\nContent");
  write_md("a.md",   "# A\nContent");
  write_txt("z.txt", "Should be ignored");
  fs::create_directories(temp_root / "sub"); // a sub‐directory with no md

  Request req = make_get_request("/docs/");
  auto res    = handler->handle_request(req);

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
  auto res    = handler->handle_request(req);

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
  auto res    = handler->handle_request(req);

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
  auto res    = handler->handle_request(req);

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
  auto res    = handler->handle_request(req);

  ASSERT_EQ(res->result(), http::status::ok);
  std::string body = res->body();

  size_t pos_alpha = body.find("alpha.md");
  size_t pos_beta  = body.find("beta.md");
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
  auto res    = handler->handle_request(req);
  EXPECT_EQ(res->result(), http::status::not_found);
}

/* ------------------------------------------------------------------ */
/* Test: Path traversal (“../”) on directory listing → 404              */
/* ------------------------------------------------------------------ */
TEST_F(MarkdownHandlerDirectoryTest, PathTraversalIsBlocked) {
  // Even if parent exists, “../” should be blocked
  Request req = make_get_request("/docs/../");
  auto res    = handler->handle_request(req);
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

