# fluidcore: GUI-independent simulation & geometry library (pure C++23, unit tested).
add_library(fluidcore STATIC
  src/FlowField.cpp
  src/LbmSolver.cpp
  src/Mesh.cpp
  src/MeshSimplify.cpp
  src/ObjLoader.cpp
  src/SceneLayout.cpp
  src/ThreadPool.cpp
  src/TransferFunction.cpp
  src/Voxelizer.cpp
)
add_library(fluid::core ALIAS fluidcore)

target_include_directories(fluidcore PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(fluidcore PUBLIC cxx_std_23)

find_package(Threads REQUIRED)
target_link_libraries(fluidcore PUBLIC Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(fluidcore PRIVATE -Wall -Wextra -Wpedantic $<$<CONFIG:Release>:-O3>)
endif()
