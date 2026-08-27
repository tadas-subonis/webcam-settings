cmake_minimum_required(VERSION 3.24)

project(WebcamSettings VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Multimedia)

qt_standard_project_setup()

set(WEBCAM_SETTINGS_SOURCES
    src/main.cpp
    src/camera_control.cpp
    src/control_row.cpp
    src/main_window.cpp
    src/profile_store.cpp
)

if(WIN32)
    list(APPEND WEBCAM_SETTINGS_SOURCES src/windows_control_backend.cpp src/windows_camera_usage.cpp)
elseif(APPLE)
    enable_language(OBJCXX)
    list(APPEND WEBCAM_SETTINGS_SOURCES src/macos_control_backend.mm src/macos_camera_usage.mm)
    set_source_files_properties(src/macos_control_backend.mm PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
elseif(UNIX)
    list(APPEND WEBCAM_SETTINGS_SOURCES src/linux_control_backend.cpp src/linux_camera_usage.cpp)
else()
    message(FATAL_ERROR "Webcam Settings supports Windows, macOS, and Linux")
endif()

qt_add_executable(webcam-settings WIN32 MACOSX_BUNDLE ${WEBCAM_SETTINGS_SOURCES})

target_include_directories(webcam-settings PRIVATE src)
target_compile_definitions(webcam-settings PRIVATE WEBCAM_SETTINGS_VERSION="${PROJECT_VERSION}")
target_link_libraries(webcam-settings PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
)

set(WEBCAM_SETTINGS_CLI_SOURCES
    src/cli_main.cpp
    src/camera_control.cpp
    src/profile_store.cpp
)
if(WIN32)
    list(APPEND WEBCAM_SETTINGS_CLI_SOURCES src/windows_control_backend.cpp src/windows_camera_usage.cpp)
elseif(APPLE)
    list(APPEND WEBCAM_SETTINGS_CLI_SOURCES src/macos_control_backend.mm src/macos_camera_usage.mm)
elseif(UNIX)
    list(APPEND WEBCAM_SETTINGS_CLI_SOURCES src/linux_control_backend.cpp src/linux_camera_usage.cpp)
endif()

qt_add_executable(webcam-settings-cli ${WEBCAM_SETTINGS_CLI_SOURCES})
target_include_directories(webcam-settings-cli PRIVATE src)
target_compile_definitions(webcam-settings-cli PRIVATE WEBCAM_SETTINGS_VERSION="${PROJECT_VERSION}")
target_link_libraries(webcam-settings-cli PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia)

if(WIN32)
    target_link_libraries(webcam-settings PRIVATE strmiids ole32 oleaut32 mfplat mf mfuuid)
    target_link_libraries(webcam-settings-cli PRIVATE strmiids ole32 oleaut32 mfplat mf mfuuid)
    target_compile_definitions(webcam-settings PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_definitions(webcam-settings-cli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(APPLE)
    target_link_libraries(webcam-settings PRIVATE
        "-framework AVFoundation"
        "-framework Foundation"
    )
    target_link_libraries(webcam-settings-cli PRIVATE
        "-framework AVFoundation"
        "-framework Foundation"
    )
endif()

set_target_properties(webcam-settings PROPERTIES
    OUTPUT_NAME "webcam-settings"
    MACOSX_BUNDLE_BUNDLE_NAME "Webcam Settings"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.tadas.webcam-settings"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist.in"
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(profile-store-tests
        tests/profile_store_tests.cpp
        src/camera_control.cpp
        src/profile_store.cpp
    )
    target_include_directories(profile-store-tests PRIVATE src)
    target_link_libraries(profile-store-tests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME profile-store-tests COMMAND profile-store-tests)

    set(BACKEND_TEST_SOURCES tests/backend_smoke_tests.cpp src/camera_control.cpp)
    if(WIN32)
        list(APPEND BACKEND_TEST_SOURCES src/windows_control_backend.cpp)
    elseif(APPLE)
        list(APPEND BACKEND_TEST_SOURCES src/macos_control_backend.mm)
    elseif(UNIX)
        list(APPEND BACKEND_TEST_SOURCES src/linux_control_backend.cpp)
    endif()
    qt_add_executable(backend-smoke-tests ${BACKEND_TEST_SOURCES})
    target_include_directories(backend-smoke-tests PRIVATE src)
    target_link_libraries(backend-smoke-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Test)
    if(WIN32)
        target_link_libraries(backend-smoke-tests PRIVATE strmiids ole32 oleaut32 mfplat mf mfuuid)
        target_compile_definitions(backend-smoke-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    elseif(APPLE)
        target_link_libraries(backend-smoke-tests PRIVATE
            "-framework AVFoundation"
            "-framework Foundation"
        )
    endif()
    add_test(NAME backend-smoke-tests COMMAND backend-smoke-tests)
    add_test(NAME cli-help COMMAND webcam-settings-cli --help)
endif()

install(TARGETS webcam-settings
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(TARGETS webcam-settings-cli RUNTIME DESTINATION bin)

qt_generate_deploy_app_script(
    TARGET webcam-settings
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
