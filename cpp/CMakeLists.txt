cmake_minimum_required(VERSION 3.15)
project(pyser)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to enable debug fprintfs inside the native extension. Disabled by default.
option(PYSER_ENABLE_DEBUG_PRINTS "Enable debug fprintf diagnostics in pyser native code" OFF)

find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(zstd REQUIRED)
find_package(nlohmann_json REQUIRED)
find_path(CPPCODEC_INCLUDE_DIRS "cppcodec/base32_crockford.hpp")

# Check that required headers are available and provide clearer errors when missing.
include(CheckIncludeFile)
check_include_file("openssl/ssl.h" HAVE_OPENSSL_SSL_H)
if(NOT HAVE_OPENSSL_SSL_H)
    message(WARNING "openssl/ssl.h not found in include path; ensure OpenSSL is installed and available via vcpkg or system packages")
endif()
check_include_file("zstd.h" HAVE_ZSTD_H)
if(NOT HAVE_ZSTD_H)
    message(WARNING "zstd.h not found in include path; ensure zstd is available via vcpkg or system packages")
endif()


set(SOURCES
        pyser.cpp
        pyser_deserialize.cpp
        pyser_json.cpp
        python_binding.cpp
        base64.h
)

Python3_add_library(pyser MODULE ${SOURCES})

if(PYSER_ENABLE_DEBUG_PRINTS)
    target_compile_definitions(pyser PRIVATE PYSER_ENABLE_DEBUG_PRINTS=1)
endif()

target_include_directories(pyser PRIVATE
        ${Python3_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(pyser PRIVATE
        ${Python3_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        zstd::libzstd
        nlohmann_json::nlohmann_json
)
target_include_directories(pyser PRIVATE ${CPPCODEC_INCLUDE_DIRS})

set_target_properties(pyser PROPERTIES
        PREFIX ""
        OUTPUT_NAME "pyser"
        SUFFIX "${Python3_SOABI}.so"
)

install(TARGETS pyser
        LIBRARY DESTINATION ${Python3_SITELIB}
)

# Small compile-time smoke test to verify debug prints compile when enabled.
if(PYSER_ENABLE_DEBUG_PRINTS)
    add_executable(pysr_debug_test EXCLUDE_FROM_ALL ${CMAKE_CURRENT_SOURCE_DIR}/debug_dummy.cpp)
    target_compile_definitions(pysr_debug_test PRIVATE PYSER_ENABLE_DEBUG_PRINTS=1)
endif()
