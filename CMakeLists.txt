cmake_minimum_required(VERSION 3.20)
project(lego_racers_dualsense_hook LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Use MSVC on Windows with -A Win32. The game is a 32-bit executable.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG 8af6b4acae5a9388fd742b56fa79ece89d96f823
)
FetchContent_MakeAvailable(minhook)
set_property(TARGET minhook PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(dinput SHARED native.cpp signatures.cpp dinput.def)
add_executable(probe probe.cpp signatures.cpp)
foreach(target dinput probe)
  target_link_libraries(${target} PRIVATE minhook setupapi hid winmm user32 ole32 uuid)
  target_compile_options(${target} PRIVATE /EHsc /utf-8)
endforeach()
enable_testing()
add_test(NAME native_hook_logic COMMAND probe --logic-only)
