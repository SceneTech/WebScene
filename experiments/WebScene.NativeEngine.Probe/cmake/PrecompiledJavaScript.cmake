if(NOT WEBSCENE_NATIVE_ENGINE_ENABLE_V8)
  target_sources(webscene_native_engine PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../native/webscene_precompiled_javascript_stub.cpp")
  return()
endif()
add_executable(webscene-jsc "${CMAKE_CURRENT_LIST_DIR}/../tools/precompile_javascript.cpp")
target_compile_features(webscene-jsc PRIVATE cxx_std_20)
target_link_libraries(webscene-jsc PRIVATE webscene_native_engine)
if(APPLE)
  set_target_properties(webscene-jsc PROPERTIES INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
  set_target_properties(webscene-jsc PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
add_custom_command(TARGET webscene-jsc POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${WEBSCENE_V8_ICU_DATA}" "$<TARGET_FILE_DIR:webscene-jsc>/icudtl.dat"
  VERBATIM)
if(WEBSCENE_NATIVE_ENGINE_V8_SNAPSHOT STREQUAL "bootstrap")
  add_dependencies(webscene-jsc webscene_v8_bootstrap_snapshot)
  add_custom_command(TARGET webscene-jsc POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${WEBSCENE_V8_SNAPSHOT_BLOB}" "${WEBSCENE_V8_SNAPSHOT_METADATA}" "$<TARGET_FILE_DIR:webscene-jsc>"
    VERBATIM)
endif()
if(BUILD_TESTING)
  add_executable(webscene_precompiled_javascript_tests
    "${CMAKE_CURRENT_LIST_DIR}/../tests/precompiled/native_engine_tests.cpp")
  target_compile_features(webscene_precompiled_javascript_tests PRIVATE cxx_std_20)
  target_link_libraries(webscene_precompiled_javascript_tests PRIVATE webscene_native_engine)
  add_test(NAME webscene_precompiled_javascript_tests COMMAND webscene_precompiled_javascript_tests)
  add_test(NAME webscene_precompiled_javascript_rejection COMMAND webscene_precompiled_javascript_tests --reject)
  set_tests_properties(webscene_precompiled_javascript_tests webscene_precompiled_javascript_rejection PROPERTIES TIMEOUT 30)
endif()
