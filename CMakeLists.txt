cmake_minimum_required(VERSION 3.10.0)
project(wasd-gamers)

# Make sure developers do not run cmake in the main project directory, to keep
# build artifacts from becoming clutter
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds not allowed.
    Please make a new directory (called a build directory) and run CMake from there.
    You may need to remove CMakeCache.txt." )
endif()

# Turn on debug builds if we are building in a devel shell
if (CMAKE_BUILD_TYPE STREQUAL "" AND "$ENV{DEVEL_SHELL}" STREQUAL "1")
    message(STATUS "Setting debug build type by default in devel shell")
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Output binaries to a sub directory "bin"
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Enable cmake testing
include(CTest)
enable_testing()

# Enable GoogleTest
include(GoogleTest)
add_subdirectory(/usr/src/googletest googletest)

# Enable Boost
# Use static libraries so binaries can be deployed without a full boost install
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.50 REQUIRED COMPONENTS system regex log log_setup)
message(STATUS "Boost version: ${Boost_VERSION}")

include_directories(include)

# Compile config_parser into a library
add_library(config_parser src/config_parser.cc)
target_link_libraries(config_parser PUBLIC Boost::regex Boost::log_setup Boost::log)

# Compile core webserver logic (server, session) into a library
add_library(webserver_lib src/server.cc src/session.cc src/logger.cc)
target_link_libraries(webserver_lib PUBLIC Boost::system Boost::regex Boost::log_setup Boost::log) # Ensure consumers will also link Boost

# Build the main webserver executable
add_executable(webserver src/webserver_main.cc)
target_link_libraries(webserver webserver_lib config_parser)

# Define config parser unit test executable, link its libraries, and discover its tests
add_executable(config_parser_test tests/config_parser_test.cc)
target_link_libraries(config_parser_test config_parser gtest_main)
gtest_discover_tests(config_parser_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Define webserver unit test executable, link its libraries, and discover its tests
add_executable(session_test tests/session_test.cc)
target_link_libraries(session_test webserver_lib gtest_main)
gtest_discover_tests(session_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# Define server unit test executable, link its libraries, and discover its tests
add_executable(server_test tests/server_test.cc)
target_link_libraries(server_test webserver_lib gmock gtest_main)
gtest_discover_tests(server_test WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

include(cmake/CodeCoverageReportConfig.cmake)
generate_coverage_report(TARGETS webserver_lib config_parser TESTS session_test config_parser_test server_test)

add_test(NAME integration_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND bash tests/integration_test.sh)
