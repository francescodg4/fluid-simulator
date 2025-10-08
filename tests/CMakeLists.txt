FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.15.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(run-test
  test_geometry.cpp
  test_solver.cpp
  test_visualization.cpp
)
target_link_libraries(run-test PRIVATE fluid::core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(run-test)

# Application-level tests (Qt): only when the application is built.
if(WINDTUNNEL_BUILD_APP)
  find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
  add_executable(run-app-test
    test_logging.cpp
    ../app/src/logging/Logging.cpp
    ../app/src/logging/LogModel.cpp
  )
  set_target_properties(run-app-test PROPERTIES AUTOMOC ON)
  target_include_directories(run-app-test PRIVATE ../app/src)
  target_link_libraries(run-app-test PRIVATE Qt6::Core spdlog::spdlog Catch2::Catch2WithMain)
  catch_discover_tests(run-app-test)
endif()
