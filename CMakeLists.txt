#
# webdis cmake build definition
#
cmake_minimum_required(VERSION 3.20)

project(webdis)

# Enforce out-of-source build
if (${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
  message(FATAL_ERROR "In-source builds not allowed. Please make a build directory and run CMake from there.")
endif()

# If CMAKE_BUILD_TYPE is None, set it to Debug
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE "Debug" CACHE STRING "Debug was set by default" FORCE)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

# User-controlled configuration
option(WITH_MSGPACK "Add msgpack support" ON)
option(WITH_OPENSSL "Add OpenSSL support" ON)
option(WITH_TESTS   "Build tests" ON)
option(WITH_OWN_HIREDIS "Use local hiredis" OFF)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")

include(WebdisProjectDependencies)

# Add those folders as static libraries to re-link them with tests 
set(B64_SRC
  src/b64/cencode.c
  src/b64/cencode.h
)
add_library(b64 STATIC ${B64_SRC})
target_include_directories(b64 PUBLIC src/b64)

set(SHA1_SRC
  src/sha1/sha1.c
  src/sha1/sha1.h
)
add_library(sha1 STATIC ${SHA1_SRC})
target_include_directories(sha1 PUBLIC src/sha1)

set(HTTPARSE_SRC
  src/http-parser/http_parser.c
  src/http-parser/http_parser.h
)
add_library(http_parser STATIC ${HTTPARSE_SRC})
target_include_directories(http_parser PUBLIC src/http-parser)

set(FORMATS_SRC
  src/formats/common.c
  src/formats/common.h
  src/formats/custom-type.c
  src/formats/custom-type.h
  src/formats/json.c
  src/formats/json.h
  src/formats/raw.c
  src/formats/raw.h
)
if (msgpack_FOUND)
  LIST(APPEND FORMATS_SRC
    src/formats/msgpack.c
    src/formats/msgpack.h
  )
endif()

aux_source_directory(src/jansson/src JANSSON_SRC) # TODO: can be also used from system
aux_source_directory(src/md5 MD5_SRC)
aux_source_directory(src WEBDIS_SRC)

if (WITH_OWN_HIREDIS)
  set(HIREDIS_SRC
    src/hiredis/hiredis.c
    src/hiredis/hiredis.h
    src/hiredis/sds.c
    src/hiredis/sds.h
    src/hiredis/net.c
    src/hiredis/net.h
    src/hiredis/async.c
    src/hiredis/async.h
    src/hiredis/read.c
    src/hiredis/read.h
    src/hiredis/dict.c
    src/hiredis/dict.h
    src/hiredis/alloc.c
    src/hiredis/alloc.h
    src/hiredis/sockcompat.c
    src/hiredis/sockcompat.h
  )
  if (WITH_OPENSSL)
    list(APPEND HIREDIS_SRC "src/hiredis/ssl.c")
  endif()
endif()

add_executable(${PROJECT_NAME} ${JANSSON_SRC} ${FORMATS_SRC} ${MD5_SRC} ${HIREDIS_SRC} ${WEBDIS_SRC})

target_compile_definitions(${PROJECT_NAME} PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(${PROJECT_NAME} PRIVATE src src/jansson/src src/http-parser)
target_link_libraries(${PROJECT_NAME} PRIVATE http_parser b64 sha1)
target_link_libraries(${PROJECT_NAME} PRIVATE libevent::libevent Threads::Threads)
if (NOT WITH_OWN_HIREDIS)
  target_link_libraries(${PROJECT_NAME} PRIVATE hiredis::hiredis)
endif()
if (WITH_OPENSSL)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SSL=1)
  target_link_libraries(${PROJECT_NAME} PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if (msgpack_FOUND)
  target_compile_definitions(${PROJECT_NAME} PRIVATE MSGPACK=1)
  target_link_libraries(${PROJECT_NAME} PRIVATE msgpackc)
endif()

if (WITH_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(CMakePrintHelpers)
message(STATUS "=======================================")
message(STATUS "Configured project: ${PROJECT_NAME}")
message(STATUS "======= Project variables =============")
cmake_print_variables(CMAKE_BUILD_TYPE)
cmake_print_variables(CMAKE_C_COMPILER CMAKE_C_FLAGS)
cmake_print_variables(WITH_OWN_HIREDIS WITH_MSGPACK WITH_OPENSSL WITH_TESTS)
cmake_print_properties(TARGETS ${PROJECT_NAME} PROPERTIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
message(STATUS "======= System discovered variables =============")
cmake_print_variables(OPENSSL_FOUND OPENSSL_VERSION)
if (WITH_OWN_HIREDIS)
  message(STATUS "Using local hiredis sources")
else()
  cmake_print_variables(hiredis_FOUND hiredis_hiredis_VERSION)
endif()
cmake_print_variables(libevent_FOUND libevent_libevent_VERSION)
cmake_print_variables(msgpack_FOUND msgpack_VERSION)
message(STATUS "=======================================")
