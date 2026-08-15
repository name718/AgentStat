cmake_minimum_required(VERSION 3.10)
project(agentstat C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output binary to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

include_directories(include)

file(GLOB SOURCES "src/*.c")

add_executable(agentstat ${SOURCES})
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
target_link_libraries(agentstat PRIVATE SQLite::SQLite3 Threads::Threads)

if(MSVC)
    target_compile_options(agentstat PRIVATE /W4)
else()
    target_compile_options(agentstat PRIVATE -Wall -Wextra -O2)
endif()
