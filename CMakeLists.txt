cmake_minimum_required(VERSION 3.21)

project(WindTunnel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WINDTUNNEL_BUILD_TESTS "Build the unit tests" ON)

include(FetchContent)

add_subdirectory(core)

if(WINDTUNNEL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Packaging
set(CPACK_PACKAGE_NAME "WindTunnel")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "maintainer")
set(CPACK_PACKAGE_CONTACT "maintainer@email.com")
include(CPack)
