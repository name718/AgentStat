cmake_minimum_required(VERSION 3.10)
project(agentstat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

file(GLOB SOURCES "src/*.c")

add_executable(agentstat ${SOURCES})
find_package(SQLite3 REQUIRED)
target_link_libraries(agentstat PRIVATE SQLite::SQLite3)

if(MSVC)
    target_compile_options(agentstat PRIVATE /W4)
else()
    target_compile_options(agentstat PRIVATE -Wall -Wextra -O2)
endif()
