cmake_minimum_required(VERSION 3.21)
project(SuperMidiMap VERSION 0.4.0 LANGUAGES CXX)

if (WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

set(APP_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/PadGridWidget.cpp
    src/MidiEngine.cpp
    src/MidiClip.cpp
    src/MappingProfile.cpp
)

if (WIN32)
    list(APPEND APP_SOURCES
        src/backends/WinMidiBackend.cpp
        resources/app.rc
    )
elseif (APPLE)
    list(APPEND APP_SOURCES
        src/backends/CoreMidiBackend.cpp
    )
else()
    message(FATAL_ERROR "SuperMidiMap 目前仅支持 Windows 与 macOS")
endif()

add_executable(SuperMidiMap WIN32 MACOSX_BUNDLE ${APP_SOURCES})

target_link_libraries(SuperMidiMap PRIVATE Qt6::Widgets Qt6::Network)

if (WIN32)
    target_compile_definitions(SuperMidiMap PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(SuperMidiMap PRIVATE winmm)
elseif (APPLE)
    target_link_libraries(SuperMidiMap PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
endif()

if (WIN32)
    add_executable(midi-probe
        tools/midi-probe.cpp
    )
    target_compile_definitions(midi-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(midi-probe PRIVATE winmm)
endif()
