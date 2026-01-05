cmake_minimum_required(VERSION 3.14)
project(ekz_vc_plugin)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FetchContent to grab cpp-httplib (header-only)
include(FetchContent)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG master
)
FetchContent_MakeAvailable(httplib)

# NOTE: libdatachannel brings its own copy of nlohmann/json as a dependency (deps/json).
# Avoid FetchContent'ing nlohmann_json here to prevent duplicate-target errors when
# libdatachannel also tries to create the `nlohmann_json` target.
# If you need the standalone nlohmann_json later, add a conditional fetch that avoids
# colliding with the copy vendored by libdatachannel.

# nlohmann_json is provided by FetchContent; prefer the target `nlohmann_json::nlohmann_json`.

# Fetch libdatachannel (lightweight WebRTC stack for native plugin)
FetchContent_Declare(
  libdatachannel
  GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
  GIT_TAG v0.24.0
)
# libdatachannel will fetch its submodules/deps via CMake
FetchContent_MakeAvailable(libdatachannel)

# NOTE: libdatachannel is licensed under MPL-2.0. Be mindful of the license when distributing a modified copy.

# Fetch PortAudio (cross-platform audio I/O)
FetchContent_Declare(
  portaudio
  GIT_REPOSITORY https://github.com/PortAudio/portaudio.git
  GIT_TAG master
)
FetchContent_MakeAvailable(portaudio)

# Prefer system-provided Opus when available; otherwise *don't* attempt to build it via FetchContent
# Building libopus via FetchContent can fail on minimal CI images; require the system package instead.
include(FindPkgConfig)
pkg_check_modules(OPUS opus QUIET)
if (OPUS_FOUND)
  message(STATUS "Found Opus via pkg-config")
  set(HAVE_OPUS 1)
  target_include_directories(ekz_vc_plugin PRIVATE ${OPUS_INCLUDE_DIRS})
  target_link_libraries(ekz_vc_plugin PRIVATE ${OPUS_LIBRARIES})
else()
  # fallback to basic find_library/find_path
  find_library(OPUS_LIB NAMES opus PATHS /usr/lib /usr/lib64 /usr/local/lib /opt/homebrew/lib NO_DEFAULT_PATH)
  find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h PATHS /usr/include /usr/local/include /opt/homebrew/include NO_DEFAULT_PATH)
  if (OPUS_LIB AND OPUS_INCLUDE_DIR)
    message(STATUS "Found Opus via find_library/find_path")
    set(HAVE_OPUS 1)
    target_include_directories(ekz_vc_plugin PRIVATE ${OPUS_INCLUDE_DIR})
    target_link_libraries(ekz_vc_plugin PRIVATE ${OPUS_LIB})
  else()
    message(WARNING "Opus library not found. Voice capture/encoding will be disabled. To enable, install 'libopus-dev' (Ubuntu) or 'brew install opus'.")
    set(HAVE_OPUS 0)
  endif()
endif()

# If PortAudio and Opus were found/built, define compile flags so code can conditionally compile
if (TARGET portaudio)
  set(HAVE_PORTAUDIO 1)
endif()
if (TARGET opus)
  set(HAVE_OPUS 1)
endif()

add_library(ekz_vc_plugin SHARED src/plugin.cpp src/webrtc_bridge.cpp src/webrtc_peer_manager.cpp src/event_queue.cpp src/event_api.cpp src/audio_capture.cpp src/opus_codec.cpp src/opus_decoder.cpp src/audio_playback.cpp src/webrtc_bridge.cpp src/plugin_api.h)

# Simple unit tests
add_executable(test_event_queue tests/test_event_queue.cpp src/event_queue.cpp src/event_api.cpp)
add_test(NAME test_event_queue COMMAND test_event_queue)

add_executable(test_event_callback tests/test_event_callback.cpp src/event_api.cpp)
add_test(NAME test_event_callback COMMAND test_event_callback)

add_executable(test_debug_flag tests/test_debug_flag.cpp src/event_api.cpp)
add_test(NAME test_debug_flag COMMAND test_debug_flag)


# Include the httplib headers from the fetched content
target_include_directories(ekz_vc_plugin PRIVATE ${httplib_SOURCE_DIR})

if (MSVC)
    target_compile_definitions(ekz_vc_plugin PRIVATE WINDOWS)
endif()

# Add compile definitions if dependencies are available
if (HAVE_PORTAUDIO)
  target_compile_definitions(ekz_vc_plugin PRIVATE HAVE_PORTAUDIO=1)
  target_link_libraries(ekz_vc_plugin PRIVATE portaudio_static)
endif()
if (HAVE_OPUS)
  target_compile_definitions(ekz_vc_plugin PRIVATE HAVE_OPUS=1)
  # Linking is applied above when OPUS is detected; no target link attempt here.
endif()

# Link libdatachannel (rtc::rtc) for WebRTC support
# libdatachannel provides the rtc::rtc target when built
# Prefer linking the nlohmann_json target if available; otherwise add include dir fallback.
target_link_libraries(ekz_vc_plugin PRIVATE rtc::rtc)
if (TARGET nlohmann_json::nlohmann_json)
  message(STATUS "Linking against nlohmann_json target")
  target_link_libraries(ekz_vc_plugin PRIVATE nlohmann_json::nlohmann_json)
else()
  message(STATUS "nlohmann_json target not available; adding include directory from FetchContent")
  if (DEFINED nlohmann_json_SOURCE_DIR)
    target_include_directories(ekz_vc_plugin PRIVATE ${nlohmann_json_SOURCE_DIR}/include)
  endif()
endif()

# Note: cpp-httplib is header-only. For more advanced setups consider using Boost.Beast or a dedicated WebSocket library.
