# Qualified macOS SDK compiler and C++ runtime closure.
set(WebScene_MACOS_PROFILE_SCHEMA 1)
set(WebScene_MACOS_PROFILE_NAME "homebrew-llvm-22.1.8-system-libcxx")
set(WebScene_MACOS_COMPILER_ID "Clang")
set(WebScene_MACOS_COMPILER_VERSION "22.1.8")
set(WebScene_MACOS_COMPILER_SHA256 "305697d3868243dca6e4aabf4f628df7de135d701eb4fd78f6a5d7528fef4395")
set(WebScene_MACOS_COMPILER_CONFIG_SHA256 "ac7ed4621b841f2103cf6162dff770b79b202dffe60b37ac6bb5406c5ea0d925")
set(WebScene_MACOS_LIBCXX_HEADERS_SHA256 "32a3e66e382e98c33bb6f14e2c85bf21f6bbb28c506ac0ac85d2a9d67f026281")
set(WebScene_MACOS_CLANG_HEADERS_SHA256 "9f58d4913aa8a78e52ce087f689c534c3c8fe524368857460ffb860bb18dffe6")
set(WebScene_MACOS_ARCHITECTURE "arm64")
set(WebScene_MACOS_DEPLOYMENT_TARGET "26.0")
set(WebScene_MACOS_CXX_STANDARD "20")
set(WebScene_MACOS_CXX_LIBRARY "libc++")
set(WebScene_MACOS_CXX_RUNTIME "/usr/lib/libc++.1.dylib")
set(WebScene_MACOS_SYSTEM_RUNTIME "/usr/lib/libSystem.B.dylib")

function(webscene_validate_macos_compiler compiler)
  if(NOT EXISTS "${compiler}")
    message(FATAL_ERROR "The ${WebScene_MACOS_PROFILE_NAME} compiler does not exist: ${compiler}")
  endif()
  file(SHA256 "${compiler}" _ws_compiler_sha256)
  if(NOT _ws_compiler_sha256 STREQUAL WebScene_MACOS_COMPILER_SHA256)
    message(FATAL_ERROR
      "The macOS SDK requires ${WebScene_MACOS_PROFILE_NAME}; compiler SHA-256 "
      "${_ws_compiler_sha256} does not match ${WebScene_MACOS_COMPILER_SHA256}")
  endif()
endfunction()

function(webscene_require_macos_profile)
  if(NOT APPLE OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    message(FATAL_ERROR "The ${WebScene_MACOS_PROFILE_NAME} profile requires macOS ARM64")
  endif()
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL WebScene_MACOS_COMPILER_ID OR
      NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL WebScene_MACOS_COMPILER_VERSION)
    message(FATAL_ERROR
      "The macOS SDK requires ${WebScene_MACOS_PROFILE_NAME}; found "
      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
  endif()
  webscene_validate_macos_compiler("${CMAKE_CXX_COMPILER}")
  if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL WebScene_MACOS_ARCHITECTURE)
    message(FATAL_ERROR
      "The macOS SDK requires architecture ${WebScene_MACOS_ARCHITECTURE}; "
      "found ${CMAKE_OSX_ARCHITECTURES}")
  endif()
  if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS WebScene_MACOS_DEPLOYMENT_TARGET)
    message(FATAL_ERROR
      "The macOS SDK requires minimum deployment target ${WebScene_MACOS_DEPLOYMENT_TARGET}; "
      "found ${CMAKE_OSX_DEPLOYMENT_TARGET}")
  endif()
endfunction()
