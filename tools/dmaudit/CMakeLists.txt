cmake_minimum_required(VERSION 3.15)
project(dmaudit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Standalone auditor — reuses only game/dmusic/riff.{h,cpp}.
# No Tempest, no engine, no game links.
set(DMUSIC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../game/dmusic")

add_executable(dmaudit
  dmaudit.cpp
  "${DMUSIC_DIR}/riff.cpp"
)

target_include_directories(dmaudit PRIVATE "${DMUSIC_DIR}")

if(MSVC)
  target_compile_options(dmaudit PRIVATE /W3)
endif()
