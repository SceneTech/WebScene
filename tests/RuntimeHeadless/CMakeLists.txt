cmake_minimum_required(VERSION 3.28)
project(RuntimeHeadlessContracts LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(WebScene REQUIRED CONFIG COMPONENTS Runtime WebGPU Compiler)
include(CTest)
add_executable(runtime_headless runtime.cpp)
webscene_compile_html(runtime_headless Main.html MODULE runtime.headless.ui MODE hybrid)
target_link_libraries(runtime_headless PRIVATE WebScene::Runtime WebScene::WebGPU)
add_custom_command(TARGET runtime_headless POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${WebScene_SDK_ROOT}/runtime" "$<TARGET_FILE_DIR:runtime_headless>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:WebScene::_Dawn>" "$<TARGET_FILE_DIR:runtime_headless>")
set_target_properties(runtime_headless PROPERTIES BUILD_RPATH "$ORIGIN")
add_test(NAME runtime_headless_javascript_gpu COMMAND runtime_headless)
add_test(NAME runtime_headless_disabled_policy COMMAND runtime_headless --policy-disabled)
add_test(NAME runtime_headless_software_requires_opt_in COMMAND runtime_headless --reject-software)
set_tests_properties(runtime_headless_javascript_gpu runtime_headless_disabled_policy
  runtime_headless_software_requires_opt_in PROPERTIES TIMEOUT 90)
