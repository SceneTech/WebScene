include_guard(GLOBAL)

if(NOT WIN32)
  message(FATAL_ERROR
    "This WebScene native C/C++ package is a Windows RID package and cannot be used on ${CMAKE_SYSTEM_NAME}.")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/WebSceneWindowsConfig.cmake")
