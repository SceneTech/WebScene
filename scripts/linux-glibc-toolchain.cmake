set(CMAKE_SYSTEM_NAME Linux)

# CMake re-evaluates this toolchain inside try_compile projects. Explicitly
# forward WebScene's target identity so compiler ABI checks remain cross builds.
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  WEBSCENE_LINUX_TARGET_TRIPLE
  WEBSCENE_RUST_TARGET_TRIPLE
  CMAKE_SYSROOT)

if(NOT DEFINED WEBSCENE_LINUX_TARGET_TRIPLE)
  message(FATAL_ERROR "WEBSCENE_LINUX_TARGET_TRIPLE is required")
endif()
if(NOT DEFINED CMAKE_SYSROOT OR CMAKE_SYSROOT STREQUAL "")
  message(FATAL_ERROR "CMAKE_SYSROOT is required")
endif()

if(WEBSCENE_LINUX_TARGET_TRIPLE STREQUAL "x86_64-linux-gnu")
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
elseif(WEBSCENE_LINUX_TARGET_TRIPLE STREQUAL "aarch64-linux-gnu")
  set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
  message(FATAL_ERROR "Unsupported Linux target triple: ${WEBSCENE_LINUX_TARGET_TRIPLE}")
endif()

# The pinned sysroots use Debian multiarch directories. CMake does not always
# infer these while cross-compiling, so make the target layout available to all
# find_package/find_library calls instead of resolving libraries from the host.
set(CMAKE_LIBRARY_ARCHITECTURE "${WEBSCENE_LINUX_TARGET_TRIPLE}")
list(APPEND CMAKE_SYSTEM_LIBRARY_PATH
  "/lib/${WEBSCENE_LINUX_TARGET_TRIPLE}"
  "/usr/lib/${WEBSCENE_LINUX_TARGET_TRIPLE}")
list(APPEND CMAKE_SYSTEM_INCLUDE_PATH
  "/usr/include/${WEBSCENE_LINUX_TARGET_TRIPLE}"
  "/usr/include")

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET "${WEBSCENE_LINUX_TARGET_TRIPLE}")
set(CMAKE_CXX_COMPILER_TARGET "${WEBSCENE_LINUX_TARGET_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
