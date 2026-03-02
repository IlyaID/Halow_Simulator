cmake_minimum_required(VERSION 3.16)
project(halow_sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-maybe-uninitialized")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -g -O0")

add_executable(halow-sim
  src/main.cc
  src/Edca.cc
)

target_include_directories(halow-sim PRIVATE include)

if (MSVC)
  target_compile_options(halow-sim PRIVATE /W4 /O2)
else()
  target_compile_options(halow-sim PRIVATE -Wall -Wextra -Wpedantic -O3)
endif()
