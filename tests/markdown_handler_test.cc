// tests/markdown_handler_test.cc
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "handlers/markdown_handler.h"
#include "mock_file_system.h"

using namespace wasd::http;
namespace http = boost::beast::http;
namespace fs    = std::filesystem;

/* ------------------------------------------------------------------ */
/*  Helper: write a file onto the real disk so std::filesystem tests  */
/*          inside MarkdownHandler succeed in every environment.      */
/* ------------------------------------------------------------------ */
static void write_real_file(const std::string& path, const std::string& body) {
  fs::create_directories(fs::path(path).parent_path());   // mkdir -p
  std::ofstream ofs(path, std::ios::trunc);
  ofs << body;
}

/* ================================================================== */
/*  Test 1: ?raw=1 should return the untouched Markdown + right MIME  */
/* ================================================================== */
TEST(MarkdownHandler, RawModeServesPlainMarkdown) {
  const std::string root      = "/tmp/docs";
  const std::string md_path   = root + "/sample.md";
  const std::string md_body   = "# Hello\nRaw *Markdown*.";

  /* 1 ─ mock & real FS both get the file */
  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_path, md_body);
  write_real_file(md_path, md_body);

  /* 2 ─ build handler */
  markdown_handler handler("/docs", root, /*template*/"", mock_fs);

  /* 3 ─ craft request */
  Request req;
  req.method(http::verb::get);
  req.target("/docs/sample.md?raw=1");
  req.version(11);

  /* 4 ─ call & assert */
  auto res = handler.handle_request(req);
  ASSERT_EQ(res->result(), http::status::ok);
  ASSERT_EQ(res->at(http::field::content_type), "text/markdown");
  ASSERT_EQ(res->body(), md_body);
}

/* ================================================================== */
/*  Test 2: normal path still returns HTML                            */
/* ================================================================== */
TEST(MarkdownHandler, NormalModeStillReturnsHTML) {
  const std::string root      = "/tmp/docs";
  const std::string md_path   = root + "/sample.md";
  const std::string md_body   = "# Hello\nRaw *Markdown*.";

  auto mock_fs = std::make_shared<MockFileSystem>();
  mock_fs->write_file(md_path, md_body);
  write_real_file(md_path, md_body);

  markdown_handler handler("/docs", root, /*template*/"", mock_fs);

  Request req;
  req.method(http::verb::get);
  req.target("/docs/sample.md");   // ← no ?raw=1
  req.version(11);

  auto res = handler.handle_request(req);
  ASSERT_EQ(res->result(), http::status::ok);
  ASSERT_EQ(res->at(http::field::content_type), "text/html");

  /* very loose HTML sanity check */
  ASSERT_NE(res->body().find("<p>"), std::string::npos);
}
