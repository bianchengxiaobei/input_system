add_executable(insys_demo
    main.cpp
    vendor/rmlui/RmlUi_Renderer_GL3.cpp
)

target_include_directories(insys_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/rmlui
)

target_link_libraries(insys_demo PRIVATE
    insys_backend_glfw
    insys_ui_rmlui
    glfw
    RmlUi::RmlUi
)

target_compile_options(insys_demo PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_custom_command(TARGET insys_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:insys_demo>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:insys_demo>
            $<TARGET_FILE_DIR:insys_demo>
    COMMAND_EXPAND_LISTS
)
