# Loaded through CMAKE_PROJECT_Dawn_INCLUDE. Keep Dawn's bundled dependencies
# out of V8's symbol namespace without modifying the pinned upstream source.
function(webscene_isolate_dawn)
    if(NOT TARGET webgpu_dawn OR NOT DAWN_BUILD_MONOLITHIC_LIBRARY STREQUAL "SHARED")
        message(FATAL_ERROR "WebScene requires the shared Dawn monolith")
    endif()
    foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
        get_target_property(definitions webgpu_dawn_objects ${property})
        if(definitions)
            list(REMOVE_ITEM definitions DAWN_NATIVE_SHARED_LIBRARY)
            set_property(TARGET webgpu_dawn_objects PROPERTY ${property} "${definitions}")
        endif()
    endforeach()
    if(UNIX AND NOT APPLE)
        # This source is compiled inside Dawn so it can use the pinned private
        # types. Consumers receive only its versioned C ABI header.
        target_sources(dawn_native_objects PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/dawn_native_device.cpp")
        target_include_directories(dawn_native_objects PRIVATE
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}"
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../experiments/WebScene.NativeEngine.Probe/native/graphics")
        target_compile_definitions(dawn_native_objects PRIVATE
            WEBSCENE_DAWN_NATIVE_DEVICE_BRIDGE=1)
    endif()
    if(APPLE)
        file(WRITE "${CMAKE_BINARY_DIR}/webscene-dawn.exports" "_wgpu*\n")
        target_link_options(webgpu_dawn PRIVATE "LINKER:-exported_symbols_list,${CMAKE_BINARY_DIR}/webscene-dawn.exports")
    elseif(UNIX)
        file(WRITE "${CMAKE_BINARY_DIR}/webscene-dawn.exports"
            "{ global: wgpu*; websceneDawnQueryVulkanDeviceV1; local: *; };\n")
        target_link_options(webgpu_dawn PRIVATE "LINKER:--version-script=${CMAKE_BINARY_DIR}/webscene-dawn.exports")
    endif()
    # Windows exports only functions decorated by WGPU_SHARED_LIBRARY;
    # removing DAWN_NATIVE_SHARED_LIBRARY above excludes the C++ native API.
endfunction()
cmake_language(DEFER CALL webscene_isolate_dawn)
