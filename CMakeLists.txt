cmake_minimum_required(VERSION 3.21)
project(RegionRecord LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(X11 REQUIRED)
endif()

# dev uses system ffmpeg (pkg-config); release-static uses vcpkg's FindFFMPEG
find_package(PkgConfig)
if(PkgConfig_FOUND AND NOT VCPKG_TOOLCHAIN)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
        libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample)
    add_library(rr_ffmpeg INTERFACE)
    target_link_libraries(rr_ffmpeg INTERFACE PkgConfig::FFMPEG)
else()
    find_package(FFMPEG REQUIRED)
    add_library(rr_ffmpeg INTERFACE)
    target_include_directories(rr_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(rr_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})
    # vcpkg's static ffmpeg alsa feature compiles in libavdevice/alsa.c (referencing snd_* symbols),
    # but FindFFMPEG does not propagate this external dependency. Explicitly link the static libasound.a
    # in the same vcpkg directory to keep a single static binary (the toolchain's find root prefers .a).
    find_library(ASOUND_LIBRARY NAMES asound)
    if(ASOUND_LIBRARY)
        target_link_libraries(rr_ffmpeg INTERFACE ${ASOUND_LIBRARY})
    endif()
endif()

# Per-OS screen-capture backend (selected at runtime by rr::createFrameSource).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_CAPTURE_SRC src/recording/X11FrameSource.cpp)
    # WaylandFrameSource.cpp is added here in Phase 2.
elseif(WIN32)
    set(PLATFORM_CAPTURE_SRC src/recording/WindowsFrameSource.cpp)
elseif(APPLE)
    set(PLATFORM_CAPTURE_SRC)  # MacFrameSource.mm: Phase 4
endif()

add_library(rr_core STATIC
    src/ffmpeg_probe.cpp
    src/recording/types.cpp
    src/recording/Mp4Encoder.cpp
    src/recording/FrameSource.cpp
    ${PLATFORM_CAPTURE_SRC}
    src/recording/AudioSource.cpp
    src/recording/Recorder.cpp
)
target_include_directories(rr_core PUBLIC src)
target_link_libraries(rr_core PUBLIC rr_ffmpeg Qt6::Core)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # X11FrameSource captures via libX11 + MIT-SHM (libXext); FFmpeg's x11grab is
    # unavailable in the static build, so we grab pixels ourselves.
    target_include_directories(rr_core PUBLIC ${X11_INCLUDE_DIR})
    target_link_libraries(rr_core PUBLIC ${X11_LIBRARIES} ${X11_Xext_LIB})
elseif(WIN32)
    # WindowsFrameSource captures via GDI BitBlt into a DIB section.
    target_link_libraries(rr_core PUBLIC gdi32 user32)
endif()

add_executable(RegionRecord src/main.cpp)
target_include_directories(RegionRecord PRIVATE src)
target_link_libraries(RegionRecord PRIVATE rr_ui)

add_executable(ui_snapshot tools/ui_snapshot.cpp)
target_include_directories(ui_snapshot PRIVATE src)
target_link_libraries(ui_snapshot PRIVATE rr_ui)

enable_testing()
add_executable(test_ffmpeg_probe tests/test_ffmpeg_probe.cpp)
target_link_libraries(test_ffmpeg_probe PRIVATE rr_core Qt6::Test)
add_test(NAME test_ffmpeg_probe COMMAND test_ffmpeg_probe)

add_executable(test_types tests/recording/test_types.cpp)
target_link_libraries(test_types PRIVATE rr_core Qt6::Test)
add_test(NAME test_types COMMAND test_types)

add_library(rr_core_test_util STATIC tests/recording/mp4_probe.cpp)
target_include_directories(rr_core_test_util PUBLIC tests)
target_link_libraries(rr_core_test_util PUBLIC rr_ffmpeg)

add_executable(test_mp4_encoder tests/recording/test_mp4_encoder.cpp)
target_link_libraries(test_mp4_encoder PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_mp4_encoder COMMAND test_mp4_encoder)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_x11_source tests/recording/test_x11_source.cpp)
    target_link_libraries(test_x11_source PRIVATE rr_core rr_core_test_util Qt6::Test)
    add_test(NAME test_x11_source COMMAND test_x11_source)
endif()

add_executable(test_recorder tests/recording/test_recorder.cpp)
target_link_libraries(test_recorder PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_recorder COMMAND test_recorder)

add_executable(test_audio_source tests/recording/test_audio_source.cpp)
target_link_libraries(test_audio_source PRIVATE rr_core rr_core_test_util Qt6::Test)
add_test(NAME test_audio_source COMMAND test_audio_source)

# ---- Application core library rr_app (business logic, depends on Qt6::Core + rr_core) ----
# Per-OS hotkey + window-picker backends (selected at runtime by the factories).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_HOTKEY_SRC src/platform/GlobalHotkeyX11.cpp)
    set(PLATFORM_PICKER_SRC src/platform/WindowPickerX11.cpp)
elseif(WIN32)
    set(PLATFORM_HOTKEY_SRC src/platform/GlobalHotkeyWindows.cpp)
    set(PLATFORM_PICKER_SRC src/platform/WindowPickerWindows.cpp)
    # macOS backends (GlobalHotkeyMac, WindowPickerMac) land in Phase 4.
endif()

add_library(rr_app STATIC
    src/app/RecordingItem.cpp
    src/app/RecordingStore.cpp
    src/app/RegionPresets.cpp
    src/app/HotkeyCombo.cpp
    src/app/RecordingController.cpp
    src/platform/GlobalHotkey.h
    src/platform/GlobalHotkeyFactory.cpp
    src/platform/WindowPickerFactory.cpp
    ${PLATFORM_HOTKEY_SRC}
    ${PLATFORM_PICKER_SRC}
)
target_include_directories(rr_app PUBLIC src)
target_link_libraries(rr_app PUBLIC Qt6::Core rr_core)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(rr_app PUBLIC ${X11_LIBRARIES})
    target_include_directories(rr_app PUBLIC ${X11_INCLUDE_DIR})
elseif(WIN32)
    # GlobalHotkeyWindows (RegisterHotKey/GetMessage) and WindowPickerWindows
    # (WindowFromPoint/GetWindowRect/cursor) both use the Win32 user32 API.
    target_link_libraries(rr_app PUBLIC user32)
endif()

add_executable(test_recording_item tests/app/test_recording_item.cpp)
target_link_libraries(test_recording_item PRIVATE rr_app Qt6::Test)
add_test(NAME test_recording_item COMMAND test_recording_item)

add_executable(test_recording_store tests/app/test_recording_store.cpp)
target_link_libraries(test_recording_store PRIVATE rr_app Qt6::Test)
add_test(NAME test_recording_store COMMAND test_recording_store)

add_executable(test_region_presets tests/app/test_region_presets.cpp)
target_link_libraries(test_region_presets PRIVATE rr_app Qt6::Test)
add_test(NAME test_region_presets COMMAND test_region_presets)

add_executable(test_hotkey_combo tests/app/test_hotkey_combo.cpp)
target_link_libraries(test_hotkey_combo PRIVATE rr_app Qt6::Test)
add_test(NAME test_hotkey_combo COMMAND test_hotkey_combo)

add_executable(test_recording_controller tests/app/test_recording_controller.cpp)
target_link_libraries(test_recording_controller PRIVATE rr_app rr_core_test_util Qt6::Test)
add_test(NAME test_recording_controller COMMAND test_recording_controller)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_global_hotkey tests/app/test_global_hotkey.cpp)
    target_link_libraries(test_global_hotkey PRIVATE rr_app Qt6::Test)
    add_test(NAME test_global_hotkey COMMAND test_global_hotkey)
endif()

# ---- GUI widget library rr_ui (depends on Widgets) ----
add_library(rr_ui STATIC
    src/ui/CountdownOverlay.cpp
    src/ui/RegionSelectorOverlay.cpp
    src/ui/frame_geometry.cpp
    src/ui/CaptureFrameWindow.cpp
    src/ui/icons.cpp
    src/ui/theme.cpp
    src/ui/MainWindow.cpp
)
target_include_directories(rr_ui PUBLIC src)
target_link_libraries(rr_ui PUBLIC rr_app Qt6::Widgets)

add_executable(test_overlays tests/app/test_overlays.cpp)
target_link_libraries(test_overlays PRIVATE rr_ui Qt6::Test)
add_test(NAME test_overlays COMMAND test_overlays)

add_executable(test_mainwindow tests/app/test_mainwindow.cpp)
target_link_libraries(test_mainwindow PRIVATE rr_ui Qt6::Test)
add_test(NAME test_mainwindow COMMAND test_mainwindow)

add_executable(test_frame_geometry tests/app/test_frame_geometry.cpp)
target_link_libraries(test_frame_geometry PRIVATE rr_ui Qt6::Test)
add_test(NAME test_frame_geometry COMMAND test_frame_geometry)
