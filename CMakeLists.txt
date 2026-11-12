cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)

    set(CMAKE_OSX_ARCHITECTURES "arm64")

else()

    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")

endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)



project(AIStyleDecorator VERSION 1.0.1)



add_compile_definitions(NOMINMAX)



if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

    set(SOURCES src/main.cpp)

elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp")

    set(SOURCES main.cpp)

else()

    file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

endif()



add_library(${PROJECT_NAME} SHARED ${SOURCES})



if (NOT DEFINED ENV{GEODE_SDK})

    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")

else()

    message(STATUS "Found Geode: $ENV{GEODE_SDK}")

endif()



add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)



if (TARGET GeodeBindings)

    target_compile_options(GeodeBindings PRIVATE

        -include "${GEODE_LOADER_PATH}/include/Geode/DefaultInclude.hpp"

        -include "${GEODE_LOADER_PATH}/include/Geode/Geode.hpp"

    )

endif()



setup_geode_mod(${PROJECT_NAME})
