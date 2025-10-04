cmake_minimum_required(VERSION 3.21)

project(WindTunnel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WINDTUNNEL_BUILD_APP "Build the Qt6 desktop application" ON)
option(WINDTUNNEL_BUILD_TESTS "Build the unit tests" ON)

include(FetchContent)

# ---- spdlog ----
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.15.3
)
# ---- argparse ----
FetchContent_Declare(
  argparse
  GIT_REPOSITORY https://github.com/p-ranav/argparse.git
  GIT_TAG v3.2
)

# ---- Sample model: extracted from the reference archive at configure time ----
set(WINDTUNNEL_MODEL_ARCHIVE "${CMAKE_CURRENT_SOURCE_DIR}/references/25-mustang-13.04.2021-obj.zip")
set(WINDTUNNEL_ASSET_DIR "${CMAKE_BINARY_DIR}/assets")
set(WINDTUNNEL_DEFAULT_MODEL "${WINDTUNNEL_ASSET_DIR}/Mustang 13.04.2021 Compressed.obj")
if(EXISTS "${WINDTUNNEL_MODEL_ARCHIVE}" AND NOT EXISTS "${WINDTUNNEL_DEFAULT_MODEL}")
  message(STATUS "Extracting sample model to ${WINDTUNNEL_ASSET_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${WINDTUNNEL_MODEL_ARCHIVE}" DESTINATION "${WINDTUNNEL_ASSET_DIR}")
endif()

add_subdirectory(core)

if(WINDTUNNEL_BUILD_APP)
  FetchContent_MakeAvailable(spdlog argparse)
  add_subdirectory(app)
endif()

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
