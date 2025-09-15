FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.15.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(run-test
  test_geometry.cpp
  test_solver.cpp
)
target_link_libraries(run-test PRIVATE fluid::core Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(run-test)
