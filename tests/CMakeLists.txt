add_executable(insys_tests
    test_fourcc.cpp
    test_event.cpp
    test_state_block.cpp
    test_event_stream.cpp
    test_state_buffers.cpp
    test_layout_builder.cpp
    test_control_path.cpp
    test_device_gamepad.cpp
    test_added_controls.cpp
    test_added_devices.cpp
    test_sensors_touch.cpp
    test_enhanced_touch.cpp
    test_onscreen.cpp
    test_gestures.cpp
    test_remoting.cpp
    test_remoting_tcp.cpp
    test_remoting_udp.cpp
    test_remoting_websocket.cpp
    test_descriptor_parser.cpp
    test_processors.cpp
    test_binding_resolver.cpp
    test_action_state.cpp
    test_input_user.cpp
    test_interactions.cpp
    test_rebinding.cpp
    test_asset_loader.cpp
    test_binding_override.cpp
    test_listeners_trace.cpp
    test_debugger_model.cpp
    test_performance.cpp
    test_stability.cpp
    test_xinput_mapping.cpp
    test_evdev_mapping.cpp
    test_evdev_device.cpp
    test_evdev_reader.cpp
    test_evdev_capture.cpp
    test_evdev_hotplug.cpp
    test_evdev_backend.cpp
    test_linux_evdev.cpp
    test_inotify_reader.cpp
    test_inotify_monitor.cpp
    test_gamecontroller_mapping.cpp
    test_gamecontroller_backend.cpp
    test_sensor_backend.cpp
    test_touch_backend.cpp
    test_mobile_input.cpp
    test_mobile_input_queue.cpp
    test_android_sensor.cpp
    test_android_input.cpp
    test_ios_motion.cpp
    test_ios_coremotion.cpp
    test_ios_step_counter.cpp
    test_ios_touch.cpp
    test_device_commands.cpp
    test_hid_descriptor.cpp
    test_hid_gamepad.cpp
    test_hid_backend.cpp
    test_hid_report_queue.cpp
)

target_include_directories(insys_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(insys_tests PRIVATE insys Catch2::Catch2WithMain)

add_test(NAME insys_tests COMMAND insys_tests)

if(TARGET insys_editor_core)
    add_executable(insys_editor_tests
        test_editor_session.cpp
        test_editor_catalog.cpp
        test_editor_preview.cpp
        test_editor_hid_tools.cpp
        test_editor_protocol.cpp
        test_editor_server.cpp
    )
    target_include_directories(insys_editor_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(insys_editor_tests PRIVATE
        insys_editor_core
        Catch2::Catch2WithMain
    )
    add_test(NAME insys_editor_tests COMMAND insys_editor_tests)
endif()

if(TARGET insys_ui_rmlui)
    add_executable(insys_ui_tests
        test_rmlui_event_sink.cpp
        test_rmlui_key_map.cpp
        test_rmlui_system_interface.cpp
        test_rmlui_adapter.cpp
    )
    target_link_libraries(insys_ui_tests PRIVATE
        insys_ui_rmlui
        Catch2::Catch2WithMain
    )
    add_test(NAME insys_ui_tests COMMAND insys_ui_tests)
endif()



























