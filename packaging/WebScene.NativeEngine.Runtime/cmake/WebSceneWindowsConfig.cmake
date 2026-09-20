include_guard(GLOBAL)

if(CMAKE_VERSION VERSION_LESS 3.19)
  message(FATAL_ERROR "WebScene's Windows package requires CMake 3.19 or newer.")
endif()
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/WebSceneWindowsMetadata.cmake")
  message(FATAL_ERROR "The WebScene Windows package metadata is missing.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/WebSceneWindowsMetadata.cmake")

if(NOT "${WebScene_PACKAGE_RID}" MATCHES "^win-(x64|arm64)$")
  message(FATAL_ERROR "Unsupported WebScene Windows package RID '${WebScene_PACKAGE_RID}'.")
endif()
if(NOT "${WebScene_PACKAGE_ARCHITECTURE}" MATCHES "^(x64|arm64)$")
  message(FATAL_ERROR
    "Unsupported WebScene Windows package architecture '${WebScene_PACKAGE_ARCHITECTURE}'.")
endif()
if(("${WebScene_PACKAGE_RID}" STREQUAL "win-x64" AND NOT "${WebScene_PACKAGE_ARCHITECTURE}" STREQUAL "x64")
    OR ("${WebScene_PACKAGE_RID}" STREQUAL "win-arm64" AND NOT "${WebScene_PACKAGE_ARCHITECTURE}" STREQUAL "arm64"))
  message(FATAL_ERROR
    "WebScene package RID '${WebScene_PACKAGE_RID}' conflicts with architecture '${WebScene_PACKAGE_ARCHITECTURE}'.")
endif()
if(NOT "${WebScene_PACKAGE_ABI_VERSION}" STREQUAL "3")
  message(FATAL_ERROR "Unsupported WebScene native ABI '${WebScene_PACKAGE_ABI_VERSION}'; expected 3.")
endif()

set(_webscene_consumer_architecture "")
if(CMAKE_GENERATOR_PLATFORM MATCHES "^(x64|X64|amd64|AMD64)$")
  set(_webscene_consumer_architecture "x64")
elseif(CMAKE_GENERATOR_PLATFORM MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
  set(_webscene_consumer_architecture "arm64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|X86_64|amd64|AMD64)$")
  set(_webscene_consumer_architecture "x64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|ARM64|aarch64|AARCH64)$")
  set(_webscene_consumer_architecture "arm64")
endif()
if(NOT _webscene_consumer_architecture)
  message(FATAL_ERROR
    "WebScene cannot determine the Windows target architecture from generator platform "
    "'${CMAKE_GENERATOR_PLATFORM}' and system processor '${CMAKE_SYSTEM_PROCESSOR}'.")
endif()
if(NOT "${_webscene_consumer_architecture}" STREQUAL "${WebScene_PACKAGE_ARCHITECTURE}")
  message(FATAL_ERROR
    "WebScene package architecture '${WebScene_PACKAGE_ARCHITECTURE}' does not match "
    "consumer architecture '${_webscene_consumer_architecture}'.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "WebScene Windows native packages require a 64-bit consumer.")
endif()

get_filename_component(_webscene_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(_webscene_package_root "${_webscene_prefix}/../.." ABSOLUTE)
set(_webscene_runtime_directories
  "${_webscene_prefix}/bin"
  "${_webscene_package_root}/runtimes/${WebScene_PACKAGE_RID}/native")
set(_webscene_complete_runtime_directories)
foreach(_webscene_runtime_candidate IN LISTS _webscene_runtime_directories)
  set(_webscene_candidate_library
    "${_webscene_runtime_candidate}/webscene_native_engine.dll")
  set(_webscene_candidate_manifest
    "${_webscene_runtime_candidate}/webscene-native-runtime.json")
  if(EXISTS "${_webscene_candidate_library}"
      AND EXISTS "${_webscene_candidate_manifest}")
    list(APPEND _webscene_complete_runtime_directories
      "${_webscene_runtime_candidate}")
  elseif(EXISTS "${_webscene_candidate_library}"
      OR EXISTS "${_webscene_candidate_manifest}")
    message(FATAL_ERROR
      "The WebScene Windows C/C++ Runtime layout is incomplete at "
      "${_webscene_runtime_candidate}; the DLL and manifest must move together.")
  endif()
endforeach()
list(LENGTH _webscene_complete_runtime_directories
  _webscene_complete_runtime_directory_count)
if(NOT _webscene_complete_runtime_directory_count EQUAL 1)
  message(FATAL_ERROR
    "The WebScene Windows C/C++ package must contain exactly one complete "
    "Runtime layout (flattened installed SDK or NuGet); found "
    "${_webscene_complete_runtime_directory_count}.")
endif()
list(GET _webscene_complete_runtime_directories 0 _webscene_runtime_directory)
set(_webscene_runtime
  "${_webscene_runtime_directory}/webscene_native_engine.dll")
set(_webscene_manifest
  "${_webscene_runtime_directory}/webscene-native-runtime.json")
set(_webscene_implib "${_webscene_prefix}/lib/webscene_native_engine.lib")
set(_webscene_header "${_webscene_prefix}/include/webscene_native_engine.h")
foreach(_webscene_required IN ITEMS
    "${_webscene_runtime}" "${_webscene_manifest}" "${_webscene_implib}" "${_webscene_header}")
  if(NOT EXISTS "${_webscene_required}")
    message(FATAL_ERROR "The WebScene Windows C/C++ package is incomplete: ${_webscene_required}")
  endif()
endforeach()

file(READ "${_webscene_manifest}" _webscene_manifest_json)
function(_webscene_manifest_value output member)
  string(JSON _webscene_value ERROR_VARIABLE _webscene_error
    GET "${_webscene_manifest_json}" "${member}")
  if(NOT _webscene_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR
      "The WebScene runtime manifest is missing or has invalid '${member}' metadata: ${_webscene_error}")
  endif()
  set(${output} "${_webscene_value}" PARENT_SCOPE)
endfunction()
_webscene_manifest_value(_webscene_manifest_rid runtimeIdentifier)
_webscene_manifest_value(_webscene_manifest_architecture architecture)
_webscene_manifest_value(_webscene_manifest_abi abiVersion)
_webscene_manifest_value(_webscene_manifest_runtime_name fileName)
_webscene_manifest_value(_webscene_manifest_implib_name importLibraryFileName)
_webscene_manifest_value(_webscene_manifest_header_name cHeaderFileName)
_webscene_manifest_value(_webscene_manifest_runtime_sha256 sha256)
_webscene_manifest_value(_webscene_manifest_implib_sha256 importLibrarySha256)
_webscene_manifest_value(_webscene_manifest_header_sha256 cHeaderSha256)
if(NOT "${_webscene_manifest_rid}" STREQUAL "${WebScene_PACKAGE_RID}"
    OR NOT "${_webscene_manifest_architecture}" STREQUAL "${WebScene_PACKAGE_ARCHITECTURE}"
    OR NOT "${_webscene_manifest_abi}" STREQUAL "${WebScene_PACKAGE_ABI_VERSION}"
    OR NOT "${_webscene_manifest_runtime_name}" STREQUAL "webscene_native_engine.dll"
    OR NOT "${_webscene_manifest_implib_name}" STREQUAL "webscene_native_engine.lib"
    OR NOT "${_webscene_manifest_header_name}" STREQUAL "webscene_native_engine.h")
  message(FATAL_ERROR "WebScene CMake metadata does not match its runtime manifest.")
endif()
file(SHA256 "${_webscene_runtime}" _webscene_runtime_sha256)
file(SHA256 "${_webscene_implib}" _webscene_implib_sha256)
file(SHA256 "${_webscene_header}" _webscene_header_sha256)
string(TOLOWER "${_webscene_manifest_runtime_sha256}" _webscene_manifest_runtime_sha256)
string(TOLOWER "${_webscene_manifest_implib_sha256}" _webscene_manifest_implib_sha256)
string(TOLOWER "${_webscene_manifest_header_sha256}" _webscene_manifest_header_sha256)
if(NOT "${_webscene_runtime_sha256}" STREQUAL "${_webscene_manifest_runtime_sha256}"
    OR NOT "${_webscene_implib_sha256}" STREQUAL "${_webscene_manifest_implib_sha256}"
    OR NOT "${_webscene_header_sha256}" STREQUAL "${_webscene_manifest_header_sha256}")
  message(FATAL_ERROR "WebScene Windows C/C++ package hashes do not match its runtime manifest.")
endif()

if(TARGET WebScene::Runtime)
  message(FATAL_ERROR
    "A WebScene::Runtime target already exists; its package identity cannot be verified.")
endif()
add_library(WebScene::Runtime SHARED IMPORTED GLOBAL)
set_target_properties(WebScene::Runtime PROPERTIES
  IMPORTED_LOCATION "${_webscene_runtime}"
  IMPORTED_IMPLIB "${_webscene_implib}"
  INTERFACE_INCLUDE_DIRECTORIES "${_webscene_prefix}/include")

set(WebScene_SDK_ROOT "${_webscene_prefix}")
set(WebScene_RUNTIME_DIRECTORY "${_webscene_runtime_directory}")
if(_webscene_runtime_directory STREQUAL "${_webscene_prefix}/bin")
  set(WebScene_RUNTIME_LAYOUT "installed-sdk")
else()
  set(WebScene_RUNTIME_LAYOUT "nuget")
endif()
set(WebScene_RUNTIME_RID "${WebScene_PACKAGE_RID}")
set(WebScene_RUNTIME_ARCHITECTURE "${WebScene_PACKAGE_ARCHITECTURE}")
set(WebScene_RUNTIME_ABI_VERSION "${WebScene_PACKAGE_ABI_VERSION}")
set(WebScene_FOUND TRUE)

unset(_webscene_consumer_architecture)
unset(_webscene_header)
unset(_webscene_header_sha256)
unset(_webscene_implib)
unset(_webscene_implib_sha256)
unset(_webscene_manifest)
unset(_webscene_manifest_abi)
unset(_webscene_manifest_architecture)
unset(_webscene_manifest_header_sha256)
unset(_webscene_manifest_header_name)
unset(_webscene_manifest_implib_sha256)
unset(_webscene_manifest_implib_name)
unset(_webscene_manifest_json)
unset(_webscene_manifest_rid)
unset(_webscene_manifest_runtime_sha256)
unset(_webscene_manifest_runtime_name)
unset(_webscene_package_root)
unset(_webscene_prefix)
unset(_webscene_required)
unset(_webscene_runtime)
unset(_webscene_runtime_candidate)
unset(_webscene_runtime_directories)
unset(_webscene_runtime_directory)
unset(_webscene_complete_runtime_directories)
unset(_webscene_complete_runtime_directory_count)
unset(_webscene_candidate_library)
unset(_webscene_candidate_manifest)
unset(_webscene_runtime_sha256)
