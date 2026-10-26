cmake_minimum_required(VERSION 3.24)
project(cs2_project VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(profile_core src/profile.cpp)
target_include_directories(profile_core PUBLIC src)
if(MSVC)
  target_compile_options(profile_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(profile_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(profile_tests tests/profile_tests.cpp)
  target_link_libraries(profile_tests PRIVATE profile_core)
  if(MSVC)
    target_compile_options(profile_tests PRIVATE /utf-8)
  endif()
  foreach(case roundtrip malformed validation filesystem)
    add_test(NAME profile_${case} COMMAND profile_tests ${case})
  endforeach()
endif()

option(BUILD_DESKTOP "Build the Windows desktop profile editor" ${WIN32})
if(BUILD_DESKTOP)
  if(NOT WIN32)
    message(FATAL_ERROR "Desktop requires Windows or a Windows cross compiler")
  endif()
  include(FetchContent)
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06
  )
  FetchContent_MakeAvailable(imgui)
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  )
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_compile_definitions(imgui PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(imgui PUBLIC d3d11 dxgi dwmapi)
  if(MINGW)
    target_link_libraries(imgui PUBLIC d3dcompiler_47)
  else()
    target_link_libraries(imgui PUBLIC d3dcompiler)
  endif()
  add_executable(cs2_profiles WIN32 src/main_win32.cpp)
  target_link_libraries(cs2_profiles PRIVATE profile_core imgui shell32 ole32 uuid)
  if(MINGW)
    target_link_options(cs2_profiles PRIVATE -Wl,--subsystem,windows)
  endif()
  if(MSVC)
    target_compile_options(cs2_profiles PRIVATE /W4 /permissive- /utf-8)
  endif()
endif()
