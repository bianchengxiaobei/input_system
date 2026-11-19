cmake_minimum_required(VERSION 3.21)
project(insys VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(INSYS_BUILD_TESTS "Build tests" ON)
option(INSYS_BUILD_DEMO "Build acceptance demo" ON)
option(INSYS_BUILD_TOOLS "Build asset tools" ON)
option(INSYS_BUILD_EDITOR "Build the input asset editor server" OFF)
option(INSYS_BUILD_EDITOR_WEB "Build the web frontend" OFF)
if(WIN32 OR APPLE)
    option(INSYS_BUILD_EDITOR_SHELL "Build the native editor launcher" ON)
else()
    option(INSYS_BUILD_EDITOR_SHELL "Build the native editor launcher" OFF)
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(INSYS_LIBRARY_TYPE STATIC)
if(ANDROID)
    set(INSYS_LIBRARY_TYPE SHARED)
endif()

add_library(insys ${INSYS_LIBRARY_TYPE}
    src/core/fourcc.cpp
    src/core/interned_string.cpp
    src/core/string_utils.cpp
    src/core/guid.cpp
    src/core/primitive_value.cpp
    src/core/control_layout.cpp
    src/core/layout_registry.cpp
    src/core/device_matcher.cpp
    src/core/control_path.cpp
    src/core/device_builder.cpp
    src/core/default_layouts.cpp
    src/core/key.cpp
    src/core/event.cpp
    src/core/event_buffer.cpp
    src/core/event_queue.cpp
    src/core/event_stream.cpp
    src/core/state_block.cpp
    src/core/state_buffers.cpp
    src/core/state_change_monitors.cpp
    src/core/control.cpp
    src/core/device.cpp
    src/core/touch_gestures.cpp
    src/core/enhanced_touch.cpp
    src/core/touch_simulation.cpp
    src/hid/hid_descriptor.cpp
    src/hid/hid_layout.cpp
    src/hid/hid_gamepad.cpp
    src/backend/hid/hid_backend.cpp
    src/backend/hid/hid_report_queue.cpp
    src/core/manager.cpp
    src/ui/ui_pointer_input.cpp
    src/onscreen/on_screen.cpp
    src/core/system.cpp
    src/core/input_listeners.cpp
    src/core/event_trace.cpp
    src/core/remoting.cpp
    src/core/remoting_tcp.cpp
    src/core/remoting_udp.cpp
    src/core/remoting_websocket.cpp
    src/core/debugger_model.cpp
    src/actions/descriptor_parser.cpp
    src/actions/processors.cpp
    src/actions/composites.cpp
    src/actions/interactions.cpp
    src/actions/unmanaged_memory.cpp
    src/actions/action_state_memory.cpp
    src/actions/binding_resolver.cpp
    src/actions/action_state.cpp
    src/actions/action_map.cpp
    src/actions/action_asset.cpp
    src/actions/rebinding.cpp
    src/actions/control_scheme.cpp
    src/actions/input_user.cpp
    src/actions/player_input.cpp
    src/actions/binding_override.cpp
    src/asset/json.cpp
    src/asset/asset_loader.cpp
    src/asset/binary_asset.cpp
    src/backend/runtime.cpp
    src/backend/xinput/xinput_mapping.cpp
    src/backend/linux/evdev_mapping.cpp
    src/backend/linux/evdev_device.cpp
    src/backend/linux/evdev_reader.cpp
    src/backend/linux/evdev_capture.cpp
    src/backend/linux/evdev_hotplug.cpp
    src/backend/linux/evdev_backend.cpp
    src/backend/linux/linux_evdev.cpp
    src/backend/linux/inotify_reader.cpp
    src/backend/linux/inotify_monitor.cpp
    src/backend/linux/linux_inotify.cpp
    src/backend/macos/gamecontroller_mapping.cpp
    src/backend/macos/gamecontroller_backend.cpp
    src/backend/sensor_backend.cpp
    src/backend/touch_backend.cpp
    src/backend/mobile_input.cpp
    src/backend/mobile_input_queue.cpp
    src/backend/android_sensor.cpp
    src/backend/ios_motion.cpp
    src/backend/ios_coremotion.cpp
    src/backend/ios_step_counter.cpp
    src/backend/ios_touch.cpp
    src/backend/android_input.cpp
)

if(ANDROID)
    add_library(insys_android_jni SHARED
        src/backend/android/android_input_jni.cpp
    )
    target_link_libraries(insys_android_jni PRIVATE insys)
endif()

if(APPLE AND IOS)
    enable_language(OBJCXX)
    target_sources(insys PRIVATE
        src/backend/ios/ios_coremotion.mm
        src/backend/ios/ios_step_counter.mm
        src/backend/ios/ios_touch.mm
    )
    target_link_libraries(insys PRIVATE "-framework CoreMotion" "-framework UIKit")
endif()
target_include_directories(insys PUBLIC include)
target_compile_options(insys PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)
if(WIN32)
    target_link_libraries(insys PRIVATE ws2_32)
endif()

if(WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux") OR (APPLE AND NOT IOS))
    add_library(insys_hid_platform STATIC)
    target_include_directories(insys_hid_platform PRIVATE src)
    target_link_libraries(insys_hid_platform PUBLIC insys)
    target_compile_options(insys_hid_platform PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

    if(WIN32)
        target_sources(insys_hid_platform PRIVATE
            src/backend/windows/windows_hid.cpp
        )
        target_link_libraries(insys_hid_platform PRIVATE setupapi hid)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(insys_hid_platform PRIVATE
            src/backend/linux/linux_hid.cpp
        )
    elseif(APPLE AND NOT IOS)
        target_sources(insys_hid_platform PRIVATE
            src/backend/macos/macos_hid.cpp
        )
        target_link_libraries(insys_hid_platform PRIVATE
            "-framework IOKit"
            "-framework CoreFoundation"
        )
    endif()
endif()

if(INSYS_BUILD_DEMO)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(RmlUi CONFIG REQUIRED)

    add_library(insys_backend_glfw STATIC
        src/backend/glfw/glfw_backend.cpp
        src/backend/glfw/glfw_key_map.cpp
        src/backend/glfw/glfw_gamepad_map.cpp
    )
    target_include_directories(insys_backend_glfw PRIVATE src)
    target_link_libraries(insys_backend_glfw PUBLIC insys glfw)
    if(TARGET insys_hid_platform)
        target_link_libraries(insys_backend_glfw PUBLIC insys_hid_platform)
    endif()

    if(WIN32)
        target_sources(insys_backend_glfw PRIVATE
            src/backend/windows/windows_raw_input.cpp
            src/backend/windows/windows_touch.cpp
            src/backend/xinput/xinput_backend.cpp
        )
        target_link_libraries(insys_backend_glfw PRIVATE user32 xinput)
    endif()

    add_library(insys_ui_rmlui STATIC
        src/ui/ui_input_adapter.cpp
        src/ui/rmlui/file_interface.cpp
        src/ui/rmlui/rmlui_event_sink.cpp
        src/ui/rmlui/rmlui_key_map.cpp
        src/ui/rmlui/rmlui_system_interface.cpp
        src/ui/rmlui/rmlui_input_adapter.cpp
    )
    target_compile_options(insys_ui_rmlui PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)
    target_link_libraries(insys_ui_rmlui PUBLIC insys RmlUi::RmlUi)

    add_subdirectory(samples/acceptance)
endif()

if(INSYS_BUILD_EDITOR)
    add_library(insys_editor_core STATIC
        src/editor/sha256.cpp
        src/editor/editor_session.cpp
        src/editor/editor_catalog.cpp
        src/editor/editor_preview.cpp
        src/editor/editor_hid_tools.cpp
        src/editor/editor_protocol.cpp
        src/editor/editor_server.cpp
    )
    target_include_directories(insys_editor_core PRIVATE src)
    target_link_libraries(insys_editor_core PUBLIC insys)
    if(TARGET insys_hid_platform)
        target_link_libraries(insys_editor_core PUBLIC insys_hid_platform)
    endif()
    target_compile_options(insys_editor_core PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

    add_executable(insys_editor_server tools/editor/main.cpp)
    target_link_libraries(insys_editor_server PRIVATE insys_editor_core)
    if(TARGET insys_hid_platform)
        target_link_libraries(insys_editor_server PRIVATE insys_hid_platform)
    endif()
    target_compile_options(insys_editor_server PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)
    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/editor/web/dist" INSYS_EDITOR_WEB_DIST)
    target_compile_definitions(insys_editor_server PRIVATE
        "INSYS_EDITOR_WEB_DIST=\"${INSYS_EDITOR_WEB_DIST}\"")

    if(INSYS_BUILD_EDITOR_WEB)
        find_program(INSYS_NPM_EXECUTABLE npm)
        if(NOT INSYS_NPM_EXECUTABLE)
            message(FATAL_ERROR "INSYS_BUILD_EDITOR_WEB requires npm")
        endif()
        add_custom_target(insys_editor_web
            COMMAND "${INSYS_NPM_EXECUTABLE}" run build
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/editor/web"
            COMMENT "Building insys editor web frontend")
        add_dependencies(insys_editor_server insys_editor_web)
    endif()

    set(INSYS_EDITOR_WEB_SOURCE "${CMAKE_SOURCE_DIR}/editor/web/dist")
    if(INSYS_BUILD_EDITOR_SHELL)
        if(WIN32)
            include("${CMAKE_SOURCE_DIR}/cmake/WebView2.cmake")
            insys_require_webview2(INSYS_WEBVIEW2_INCLUDE_DIR
                                   INSYS_WEBVIEW2_LOADER_LIB)

            add_executable(insys_editor WIN32 tools/editor_shell/main.cpp)
            target_include_directories(insys_editor PRIVATE
                "${INSYS_WEBVIEW2_INCLUDE_DIR}")
            target_link_libraries(insys_editor PRIVATE
                insys_editor_core
                "${INSYS_WEBVIEW2_LOADER_LIB}"
                ole32
                oleaut32
                shell32
                shlwapi
                user32
                version
                advapi32)
            target_compile_definitions(insys_editor PRIVATE
                UNICODE _UNICODE
                "INSYS_EDITOR_WEB_DIST=\"${INSYS_EDITOR_WEB_DIST}\"")
            target_compile_options(insys_editor PRIVATE
                $<$<CXX_COMPILER_ID:MSVC>:/W4>)
        elseif(APPLE)
            enable_language(OBJCXX)
            find_package(Threads REQUIRED)
            add_executable(insys_editor MACOSX_BUNDLE
                tools/editor_shell/main_macos.mm
                tools/editor_shell/posix_shell.cpp)
            target_link_libraries(insys_editor PRIVATE
                insys_editor_core
                Threads::Threads
                "-framework Cocoa"
                "-framework WebKit")
            target_compile_definitions(insys_editor PRIVATE
                "INSYS_EDITOR_WEB_DIST=\"${INSYS_EDITOR_WEB_DIST}\"")
            target_compile_options(insys_editor PRIVATE
                $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)
            set_target_properties(insys_editor PROPERTIES
                OBJCXX_STANDARD 20
                OBJCXX_STANDARD_REQUIRED ON
                OBJCXX_EXTENSIONS OFF
                MACOSX_BUNDLE_BUNDLE_NAME "insys editor"
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.insys.editor"
                MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
                MACOSX_BUNDLE_INFO_PLIST
                    "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in")
        elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_package(PkgConfig REQUIRED)
            find_package(Threads REQUIRED)
            pkg_check_modules(INSYS_WEBKIT2GTK QUIET IMPORTED_TARGET
                              webkit2gtk-4.1)
            if(NOT INSYS_WEBKIT2GTK_FOUND)
                pkg_check_modules(INSYS_WEBKIT2GTK REQUIRED IMPORTED_TARGET
                                  webkit2gtk-4.0)
            endif()
            pkg_check_modules(INSYS_GTK3 REQUIRED IMPORTED_TARGET
                              gtk+-3.0)
            add_executable(insys_editor
                tools/editor_shell/main_linux.cpp
                tools/editor_shell/posix_shell.cpp)
            target_link_libraries(insys_editor PRIVATE
                insys_editor_core
                Threads::Threads
                PkgConfig::INSYS_WEBKIT2GTK
                PkgConfig::INSYS_GTK3)
            target_compile_definitions(insys_editor PRIVATE
                "INSYS_EDITOR_WEB_DIST=\"${INSYS_EDITOR_WEB_DIST}\"")
            target_compile_options(insys_editor PRIVATE
                $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)
        else()
            message(WARNING
                "INSYS_BUILD_EDITOR_SHELL is not implemented on this platform")
        endif()

        if(TARGET insys_editor)
            if(TARGET insys_editor_web)
                add_dependencies(insys_editor insys_editor_web)
            endif()
            if(EXISTS "${INSYS_EDITOR_WEB_SOURCE}")
                if(APPLE)
                    add_custom_command(TARGET insys_editor POST_BUILD
                        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                            "$<TARGET_BUNDLE_CONTENT_DIR:insys_editor>/Resources/web"
                        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                            "${INSYS_EDITOR_WEB_SOURCE}"
                            "$<TARGET_BUNDLE_CONTENT_DIR:insys_editor>/Resources/web"
                        COMMENT "Copying insys editor web assets")
                else()
                    add_custom_command(TARGET insys_editor POST_BUILD
                        COMMAND "${CMAKE_COMMAND}" -E remove_directory
                            "$<TARGET_FILE_DIR:insys_editor>/web"
                        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                            "${INSYS_EDITOR_WEB_SOURCE}"
                            "$<TARGET_FILE_DIR:insys_editor>/web"
                        COMMENT "Copying insys editor web assets")
                endif()
            else()
                message(WARNING
                    "editor/web/dist does not exist; the launcher will fall "
                    "back to the server placeholder page")
            endif()
        endif()
    endif()
endif()

if(INSYS_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    add_subdirectory(tests)
endif()

if(INSYS_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(TARGET insys_editor)
    if(APPLE)
        install(TARGETS insys_editor BUNDLE DESTINATION .)
    else()
        install(TARGETS insys_editor RUNTIME DESTINATION .)
        if(EXISTS "${INSYS_EDITOR_WEB_SOURCE}")
            install(DIRECTORY "${INSYS_EDITOR_WEB_SOURCE}/" DESTINATION web)
        endif()
    endif()
    install(FILES "${CMAKE_SOURCE_DIR}/README.md" DESTINATION .)

    if(MSVC)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
        set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
        include(InstallRequiredSystemLibraries)
        if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
                    DESTINATION .)
        endif()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(INSYS_PACKAGE_ARCH x64)
    else()
        set(INSYS_PACKAGE_ARCH x86)
    endif()
    if(WIN32)
        set(INSYS_PACKAGE_PLATFORM "windows-${INSYS_PACKAGE_ARCH}")
    elseif(APPLE)
        set(INSYS_PACKAGE_PLATFORM "macos-${CMAKE_SYSTEM_PROCESSOR}")
    else()
        set(INSYS_PACKAGE_PLATFORM "linux-${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "insys-editor")
    set(CPACK_PACKAGE_VENDOR "insys")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME
        "insys-editor-${PROJECT_VERSION}-${INSYS_PACKAGE_PLATFORM}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "insys input actions asset editor")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    include(CPack)

    if(WIN32)
        find_program(INSYS_POWERSHELL_EXECUTABLE NAMES powershell pwsh)
        if(INSYS_POWERSHELL_EXECUTABLE)
            option(INSYS_MSIX_SELF_SIGN
                   "Sign the MSIX with a generated development certificate"
                   OFF)
            set(INSYS_MSIX_PUBLISHER "CN=insys" CACHE STRING
                "MSIX identity publisher; must match the signing certificate")
            set(INSYS_MSIX_CERTIFICATE "" CACHE FILEPATH
                "Optional PFX certificate for MSIX signing")
            set(INSYS_MSIX_TIMESTAMP_URL "" CACHE STRING
                "Optional RFC 3161 timestamp URL for MSIX signing")
            set(INSYS_MSIX_STAGE "${CMAKE_BINARY_DIR}/msix/stage")
            set(INSYS_MSIX_SIGN_ARGS)
            if(INSYS_MSIX_SELF_SIGN AND NOT INSYS_MSIX_CERTIFICATE)
                list(APPEND INSYS_MSIX_SIGN_ARGS "-SelfSign")
            endif()
            if(INSYS_MSIX_CERTIFICATE)
                list(APPEND INSYS_MSIX_SIGN_ARGS
                     "-CertificatePath" "${INSYS_MSIX_CERTIFICATE}")
            endif()
            if(INSYS_MSIX_TIMESTAMP_URL)
                list(APPEND INSYS_MSIX_SIGN_ARGS
                     "-TimestampUrl" "${INSYS_MSIX_TIMESTAMP_URL}")
            endif()
            add_custom_target(insys_editor_msix
                COMMAND "${CMAKE_COMMAND}" -E remove_directory
                        "${INSYS_MSIX_STAGE}"
                COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
                        --config $<CONFIG> --prefix "${INSYS_MSIX_STAGE}"
                COMMAND "${INSYS_POWERSHELL_EXECUTABLE}" -NoProfile
                        -ExecutionPolicy Bypass
                        -File "${CMAKE_SOURCE_DIR}/packaging/msix/build-msix.ps1"
                        -InstallDir "${INSYS_MSIX_STAGE}"
                        -OutputDir "${CMAKE_BINARY_DIR}/package"
                        -Version "${PROJECT_VERSION}"
                        -Architecture "${INSYS_PACKAGE_ARCH}"
                        -Publisher "${INSYS_MSIX_PUBLISHER}"
                        ${INSYS_MSIX_SIGN_ARGS}
                DEPENDS insys_editor
                COMMENT "Packaging insys editor as MSIX")
        else()
            message(WARNING
                "PowerShell not found; the insys_editor_msix target is disabled")
        endif()
    endif()
endif()




























