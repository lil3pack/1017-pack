cmake_minimum_required(VERSION 3.22)

project(1017Pack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------- JUCE location ---------
# Pass with -DJUCE_DIR=/path/to/JUCE at configure time, or it defaults to ../JUCE
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../JUCE" CACHE PATH "Path to JUCE source tree")
endif()

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${JUCE_DIR}. "
        "Clone JUCE and pass -DJUCE_DIR=/path/to/JUCE when configuring.")
endif()

add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)

# --------- Shared options ---------
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Min macOS version")
endif()

# --------- Subprojects ---------
add_subdirectory(TrapHouse)
add_subdirectory(Lemonade)
