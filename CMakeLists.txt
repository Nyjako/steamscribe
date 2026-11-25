cmake_minimum_required(VERSION 3.20)

# Some fetched dependencies still declare old cmake_minimum_required() floors
# that newer CMake refuses outright; this keeps those configuring instead of
# requiring every contributor to pass the flag by hand.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(steamscribe VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(cmake/CPM.cmake)

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

CPMAddPackage(
  NAME cjson
  GITHUB_REPOSITORY DaveGamble/cJSON
  GIT_TAG v1.7.19
  OPTIONS "ENABLE_CJSON_TEST OFF" "ENABLE_CJSON_UTILS OFF" "BUILD_SHARED_LIBS OFF")

CPMAddPackage(
  NAME termbox2
  GITHUB_REPOSITORY termbox/termbox2
  GIT_TAG v2.7.0
  DOWNLOAD_ONLY YES)

CPMAddPackage(
  NAME inih
  GITHUB_REPOSITORY benhoyt/inih
  GIT_TAG r63
  DOWNLOAD_ONLY YES)

add_library(inih STATIC ${inih_SOURCE_DIR}/ini.c)
target_include_directories(inih PUBLIC ${inih_SOURCE_DIR})

CPMAddPackage(
  NAME goblin
  GITHUB_REPOSITORY Nyjako/GOBLIN
  GIT_TAG 0acb3b2ed3c45942b9b8bb25fb8fb6a78266f867
  DOWNLOAD_ONLY YES)

add_library(goblin INTERFACE)
target_include_directories(goblin INTERFACE ${goblin_SOURCE_DIR}/include)

file(GLOB_RECURSE STEAMSCRIBE_SRC CONFIGURE_DEPENDS src/*.c)
add_executable(steamscribe ${STEAMSCRIBE_SRC})
target_include_directories(steamscribe PRIVATE src ${termbox2_SOURCE_DIR})
target_link_libraries(steamscribe PRIVATE CURL::libcurl cjson inih Threads::Threads goblin)
target_compile_definitions(steamscribe PRIVATE STEAMSCRIBE_VERSION="${PROJECT_VERSION}")
target_compile_options(steamscribe PRIVATE -Wall -Wextra -Wpedantic)
