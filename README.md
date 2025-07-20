# Contributing to the WASD-Gamers Web Server

Welcome! This document will guide you through the process of understanding our codebase, building and testing it, and most importantly, adding new request handlers. Our server adheres to the CS 130 Common API standard.

## Table of Contents

1.  [Source Code Layout](#source-code-layout)
2.  [Build, Test, and Run](#build-test-and-run)
3.  [Adding a New Request Handler](#adding-a-new-request-handler)
    - [Overview](#overview)
    - [Step 1: Define Your Handler Class](#step-1-define-your-handler-class)
    - [Step 2: Implement the RequestHandler Interface](#step-2-implement-the-requesthandler-interface)
    - [Step 3: Hook Your Handler into `MakeHandler`](#step-3-hook-your-handler-into-makehandler)
    - [Step 4: Configure Your Handler](#step-4-configure-your-handler)
    - [Step 5: Build and Test](#step-5-build-and-test)
4.  [Core Interfaces](#core-interfaces)
    - [RequestHandler Interface](#requesthandler-interface-common-api)
    - [Example: EchoHandler (Illustrative)](#example-echohandler-illustrative)

## Source Code Layout

Our project follows a standard C++ project structure:

- `include/`: Public header files for our HTTP components, including `request_handler.h`, `echo_handler.h`, `static_handler.h`, etc. All server-specific HTTP symbols are within the `wasd::http` namespace.
- `src/`: Source code (`.cc`) files for implementation of classes and functions.
  - `webserver_main.cc`: Main entry point for the server.
  - `config_parser.cc`: Implements Nginx-style configuration parsing.
  - `server.cc`: Core server logic for listening and accepting connections.
  - `session.cc`: Manages individual client connections.
  - `handler_registry.cc`: Manages handler factories and dispatches requests.
  - `echo_handler.cc`, `static_handler.cc`, etc.: Implementations of concrete request handlers.
- `build/`: This directory is created by you for CMake build files. It's typically added to `.gitignore`.
- `tests/`: Contains unit tests and potentially integration test scripts.
- `static/`: Files and sub-folders served by `StaticHandler`.
- `docker/`: Dockerfiles and CI configuration.
- `logs/`: Runtime log output (rotated by date).

## Build, Test, and Run

Our build process is identical to the instructions from the [CS 130 website](https://www.cs130.org/guides/cmake/). Feel free to skip the Build and Test sections if you are already familiar with the process (as you should be).

**1. Build:**
Ensure you have CMake (version 3.10+) and a C++17 compatible compiler (like g++) installed.

```bash
# From the root of the project directory
mkdir -p build
cd build
cmake ..
make
```

The main server executable will typically be found in `build/bin/webserver`.

**2. Test:**
After building:

```bash
# From the 'build' directory
make test
```

The above command runs all tests, including the integation tests at `tests/integration_test.sh`.

**3. Run:**
To run the web server, you need to provide a configuration file:

```bash
# From the 'build' directory
./bin/webserver ../my_config
```

where `my_config` is the config file (there is already one with this name).

## Adding a New Request Handler

Adding a new request handler involves defining its behavior, integrating it with the server's factory system, and configuring it.

### Overview

1.  **Define** your new handler class, inheriting from `wasd::http::request_handler`.
2.  **Implement** the constructor (which will receive its parameters from `MakeHandler`) and the `void handle_request(http::request<http::string_body>& req, http::response<http::string_body>& res)` method.
3.  **Modify**
    - `src/handler_registry.cc` – add a new `if (type == "MyNewHandler") { … }` branch.
    - `CMakeLists.txt` – append `src/my_new_handler.cc` to the `add_library(request_handler_lib …)` source list so the new file is compiled.
4.  **Add** a `location` block to a configuration file to map a URL prefix to your new handler.
5.  **Rebuild** and **test** your new handler thoroughly.

### Step 1: Define Your Handler Class

Create a header file (e.g., `include/my_new_handler.h`) and a source file (e.g., `src/my_new_handler.cc`) for your new handler.

**`include/my_new_handler.h` (Example Skeleton):**

```c++
#ifndef MY_NEW_HANDLER_H
#define MY_NEW_HANDLER_H

#include "request_handler.h" // Base class interface
#include <string>

namespace wasd::http {

namespace http = boost::beast::http;

class MyNewHandler : public request_handler {
public:
    // Constructor: Takes the location prefix and any other parameters
    // parsed by MakeHandler from the handler's config block.
    // For example, if your handler needs a specific root directory or a setting:
    MyNewHandler(const std::string& location_prefix, const std::string& specific_config_value);

    // Override the core request handling method
    // Note: It modifies the response object directly and returns void.
    void handle_request(http::request<http::string_body>& req,
                        http::response<http::string_body>& res) override;

private:
    // Store any configuration passed via constructor
    std::string specific_config_value_;
};

}  // namespace wasd::http

#endif // MY_NEW_HANDLER_H
```

### Step 2: Implement the RequestHandler Interface

In your `.cc` file (e.g., `src/my_new_handler.cc`):

**Constructor:**
Your constructor receives arguments that `MakeHandler` has parsed from the handler's specific configuration block.

```c++
#include "my_new_handler.h" // Assuming it's in the same include path resolution
// #include other necessary headers like <fstream>, <sstream> if needed

using namespace wasd::http;

MyNewHandler::MyNewHandler(const std::string& location_prefix, const std::string& specific_config_value)
    : request_handler(location_prefix, "" /* Or relevant base root if applicable */), // Call base constructor
      specific_config_value_(specific_config_value)
{
    // Initialize other members if any
    // The 'location_prefix' is available via get_prefix() from base class.
    // The 'specific_config_value_' is now stored for use in handle_request.
}

void MyNewHandler::handle_request(http::request<http::string_body>& req,
                                  http::response<http::string_body>& res) {
    // 1. Implement your handler's logic here.
    //    - Examine 'req' (method, target URI, headers, body).
    //    - Use get_prefix() and potentially specific_config_value_ to guide logic.
    //    - Populate 'res' (status code, headers, body).

    // Example: Minimal response using the specific_config_value
    res.result(http::status::ok);
    res.set(http::field::content_type, "text/plain");
    res.version(req.version()); // Set HTTP version from request
    res.body() = "Hello from MyNewHandler! Configured value: " + specific_config_value_;
    res.prepare_payload(); // Important for setting Content-Length etc.
}

```

**`handle_request` Method:**

- This is the core of your handler.
- It receives `http::request<http::string_body>& req` and `http::response<http::string_body>& res`.
- It **must populate** the `res` object with the correct status code, headers (e.g., `Content-Type`), and body.
- It returns `void`.

### Step 3: Hook Your Handler into `MakeHandler`

The server creates handlers through the `MakeHandler` factory inside `src/handler_registry.cc`.  
Add a new `if (type == "MyNewHandler") { … }` branch there.

```c++
// In src/handler_registry.cc
// ... other includes ...
#include "my_new_handler.h" // Include your new handler's header

// ... within the global static MakeHandler function ...
static std::unique_ptr<request_handler>
MakeHandler(const std::string &type, const std::string &prefix, const NginxConfig *child_block) {
  if (type == "StaticHandler") {
    // expect: root <dir>;
    std::string root_dir = "/"; // Default
    if (child_block) {
      for (const auto &stmt : child_block->statements_) {
        if (stmt->tokens_.size() == 2 && stmt->tokens_[0] == "root") {
          root_dir = stmt->tokens_[1];
        }
      }
    }
    return std::make_unique<static_handler>(prefix, root_dir);
  }

  if (type == "EchoHandler") {
    return std::make_unique<echo_handler>(prefix, "" /* EchoHandler doesn't use root */);
  }

  if (type == "MyNewHandlerNameInConfig") { // This string must match the handler name in the config
    std::string my_specific_value = "default_value"; // Default
    if (child_block) {
      // Parse child_block for parameters specific to MyNewHandler
      // For example, looking for a statement like: my_param_key "some_value";
      for (const auto &stmt : child_block->statements_) {
        if (stmt->tokens_.size() == 2 && stmt->tokens_[0] == "my_param_key") {
          my_specific_value = stmt->tokens_[1];
        }
      }
    }
    return std::make_unique<MyNewHandler>(prefix, my_specific_value);
  }

  BOOST_LOG_TRIVIAL(error) << "Unknown handler type '" << type << "'";
  return nullptr;
}
```

Also, append `src/my_new_handler.cc` to the `add_library(request_handler_lib …)` source list so the new file is compiled.

### Step 4: Configure Your Handler

Add a `location` block to your server configuration file (e.g., `configs/my_config`) to tell the server when to use your new handler:

```nginx
# In your server configuration file (e.g., my_config)

location /my_feature MyNewHandlerNameInConfig {
  # Add any handler-specific arguments here, which MakeHandler will parse
  # from the child_block. For example:
  my_param_key "custom_value_for_handler";
}
```

- Replace `/my_feature` with the URL prefix you want this handler to manage.
- Replace `MyNewHandlerNameInConfig` with the string name your `MakeHandler` function recognizes for your new handler (e.g., "MyNewHandlerNameInConfig" from the example above).
- Any directives inside the `{...}` block will be available in `child_block` for `MakeHandler` to parse.

### Step 5: Build and Test

1.  Recompile your server:
    ```bash
    # From the 'build' directory
    make
    ```
2.  Run the server with the updated configuration.
3.  Test your new handler thoroughly using tools like `curl` or by writing new unit/integration tests.

### Detailed Example: `MarkdownHandler`

The `MarkdownHandler` is responsible for serving Markdown files (`.md`) from a specified root directory. It can render them as HTML (optionally using a template) or serve them as raw Markdown.

**Key Features:**

*   Serves `.md` files.
*   Renders Markdown to HTML using `cmark-gfm`.
*   Supports an optional HTML template file to wrap the rendered Markdown content. The template should contain `{{content}}` where the Markdown HTML will be injected.
*   Supports a `?raw=1` query parameter to serve the original Markdown file with `Content-Type: text/markdown`.
*   Provides directory listings for navigation, showing only `.md` files and subdirectories.
*   Implements ETag and Last-Modified based caching for both individual files and directory listings.

**Configuration Options:**

When configuring `MarkdownHandler` in your server's configuration file, you use a `location` block. The handler expects the following parameters within this block:

*   **`root <path_to_markdown_files>` (Required):** Specifies the absolute or relative path to the directory on the server's filesystem where the Markdown files are stored. This is the document root for this handler instance.
    *   Example: `root /var/www/markdown_docs;`
*   **`template <path_to_template_html>` (Optional):** Specifies the absolute or relative path to an HTML template file. If provided, the rendered Markdown content will be injected into this template where `{{content}}` is found. If omitted, the raw HTML fragment generated from the Markdown is served.
    *   Example: `template /etc/server/templates/markdown_wrapper.html;`

**Example Configuration Block:**

```nginx
# In your server configuration file (e.g., my_config)

location /docs MarkdownHandler {
  root /srv/http/markdown_files;       # Serve .md files from this directory
  template /path/to/my_template.html;  # Optional: wrap with this HTML template
}

```


**Deployment Notes for Static Content (gCloud):**

When deploying a web server that uses handlers like `StaticHandler` or `MarkdownHandler` to serve files from the filesystem (e.g., HTML, CSS, JS, images, or Markdown files), you need to ensure these static assets are available on the server instance in Google Cloud (or any other deployment environment).

1.  **Include Assets in Your Deployment Package:**
    *   If using Docker, your `Dockerfile` should `COPY` these static directories into the image.
        ```dockerfile
        # Example: Copying static assets for MarkdownHandler
        COPY --from=builder /usr/src/project/docs /docs
        COPY --from=builder /usr/src/project/templates /templates
        ```
        Then, your server configuration would point to these paths within the container (e.g., `root /app/docs_content;`).
    *   If deploying directly to a VM, ensure your deployment scripts (e.g., using `gcloud compute scp` or startup scripts) copy these asset directories to the correct locations on the VM's filesystem that your server configuration expects.

2.  **Configuration Paths:**
    *   Ensure the `root` (for `StaticHandler`, `MarkdownHandler`) and `template` (for `MarkdownHandler`) paths in your server configuration file correctly point to where these assets are located *within the deployed environment* (e.g., inside the Docker container or on the VM).

3.  **Permissions:**
    *   The user account running the web server process must have read access to these static asset directories and files.

For `MarkdownHandler` and `StaticHandler`, the primary method is to bundle the necessary files with your application deployment.

## Core Interfaces

### RequestHandler Interface (Common API)

This is the central interface all request handlers must adhere to. It ensures that the server can treat all handlers polymorphically.

**`include/request_handler.h` (Actual Interface):**

```c++
// include/request_handler.h
#ifndef REQUEST_HANDLER_H
#define REQUEST_HANDLER_H

#include <boost/beast.hpp>

namespace wasd::http {

namespace http = boost::beast::http;

class request_handler {
public:
  virtual ~request_handler();
  request_handler(const std::string &location, const std::string &root);

  virtual void handle_request(http::request<http::string_body> &req,
                              http::response<http::string_body> &res) = 0;

  std::string get_prefix() const;
  std::string get_dir() const;

private:
  std::string prefix_;
  std::string dir_;
};

} // namespace wasd::http

#endif
```

- **`request_handler(const std::string &location, const std::string &root)`**: The constructor for the base handler. Derived handlers must call this or provide their own constructor that initializes the necessary state. `location` is the matched URL prefix, and `root` is a generic parameter often used for a root directory (e.g., by `StaticHandler`).
- **`virtual void handle_request(http::request<http::string_body> &req, http::response<http::string_body> &res) = 0`**: This is the primary method a contributor needs to implement. It receives references to Boost.Beast request and response objects (or your typedefs). The handler is responsible for populating the `res` object.
- **`get_prefix()` and `get_dir()`**: Utility methods to access the location prefix and root directory configured for this handler instance.
- **Configuration**: Handlers receive their configuration (like `prefix_` and `dir_`) via their constructor, typically populated by the `MakeHandler` function in `handler_registry.cc` which parses the Nginx-style configuration block.

### Example: `EchoHandler` (Illustrative)

The `EchoHandler` is a simple handler that reflects the incoming request back to the client.

**`include/echo_handler.h` (Actual Interface):**

```c++
// include/echo_handler.h
#ifndef ECHO_HANDLER_H
#define ECHO_HANDLER_H

#include "request_handler.h"

namespace wasd::http {

namespace http = boost::beast::http;

class echo_handler : public request_handler {
public:
  using request_handler::request_handler; // Inherits base constructor
  void handle_request(http::request<http::string_body> &req,
                      http::response<http::string_body> &res) override;
};

} // namespace wasd::http

#endif
```

**`src/echo_handler.cc` (Actual Implementation):**

```c++
// src/echo_handler.cc
#include "echo_handler.h"
#include <sstream> // For serializing the request

using namespace wasd::http; // Assuming this is in your .cc for brevity

namespace http = boost::beast::http; // Alias for Boost.Beast types

void echo_handler::handle_request(http::request<http::string_body> &req,
                                  http::response<http::string_body> &res) {
  std::ostringstream oss;
  oss << req; // Serialize the incoming Boost.Beast request

  res.set(http::field::content_type, "text/plain");
  res.version(req.version()); // Set HTTP version from request
  res.result(http::status::ok);
  res.body() = oss.str();
  res.prepare_payload(); // Important for setting Content-Length etc.
}
```

## Code Style & Formatting

We follow the `.clang-format` file in the repo (root) — K&R braces, 2-space indents, 100-col limit.

### Install the tools (if you don't already have them)

```
sudo apt update
sudo apt install clang-format clang-tidy
```

### VS Code users

Add these two lines to **settings.json** so the editor picks up the project style automatically & formats on save:

```jsonc
"C_Cpp.clang_format_style": "file",
"editor.formatOnSave": true
```

### Auto-format on every commit

Create .git/hooks/pre-commit with the script below (make it executable):

```bash
#!/usr/bin/env bash

# 1. Guard: make sure clang-format is available
command -v clang-format >/dev/null 2>&1 || {
  echo >&2 "clang-format not found – install LLVM tools or skip commit.";
  exit 1
}

# 2. Find staged C/C++ files
STAGED_FILES=$(git diff --cached --name-only -- '*.h' '*.cc')
[[ -z "$STAGED_FILES" ]] && exit 0

# 3. Run clang-format on them
echo "⧗ running clang-format on staged files…"
clang-format -i --style=file $STAGED_FILES
git add $STAGED_FILES

exit 0
```

**Or, if you don't want to do all that, just run this before you commit (formatting libs still required):**

```bash
cmake --build build --target clang_format
```

## Members
*We used our UCLA email addresses on Gerrit so some member's PRs show up PRs by Devel User.

Anmol Gupta - shows up as Devel User

Shlok Jhawar

William Smith

David Han - shows up as Devel User
