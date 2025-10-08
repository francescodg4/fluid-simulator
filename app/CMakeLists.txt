find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent)

set(CMAKE_AUTOMOC ON)
configure_file(version.h.in ${CMAKE_CURRENT_BINARY_DIR}/version.h)

qt_add_executable(windtunnel
  src/main.cpp
  src/MainWindow.cpp src/MainWindow.hpp
  src/logging/LogModel.cpp src/logging/LogModel.hpp
  src/logging/Logging.cpp src/logging/Logging.hpp
  src/model/SceneDocument.cpp src/model/SceneDocument.hpp
  src/panels/FluidFlowPanel.cpp src/panels/FluidFlowPanel.hpp
  src/panels/OutlinerPanel.cpp src/panels/OutlinerPanel.hpp
  src/panels/PropertiesPanel.cpp src/panels/PropertiesPanel.hpp
  src/services/MeshProvider.cpp src/services/MeshProvider.hpp
  src/services/ReportService.cpp src/services/ReportService.hpp
  src/simulation/SimulationController.cpp src/simulation/SimulationController.hpp
  src/simulation/SimulationWorker.cpp src/simulation/SimulationWorker.hpp
  src/simulation/SimulationTypes.hpp
  src/ui/Theme.cpp src/ui/Theme.hpp
  src/viewport/OrbitCamera.cpp src/viewport/OrbitCamera.hpp
  src/viewport/OverlayItems.cpp src/viewport/OverlayItems.hpp
  src/viewport/SceneRenderer.cpp src/viewport/SceneRenderer.hpp
  src/viewport/ViewportView.cpp src/viewport/ViewportView.hpp
  src/widgets/CollapsibleSection.cpp src/widgets/CollapsibleSection.hpp
  src/widgets/TimelineWidget.cpp src/widgets/TimelineWidget.hpp
  src/widgets/TransferFunctionEditor.cpp src/widgets/TransferFunctionEditor.hpp
  src/widgets/ValueField.cpp src/widgets/ValueField.hpp
)

set(WINDTUNNEL_SHADERS
  background.frag composite.frag floor.frag floor.vert fullscreen.vert lines.frag lines.vert mesh.frag mesh.vert
  particles.frag particles.vert slice.frag slice.vert streamline.frag streamline.vert volume.frag volume.vert
)
list(TRANSFORM WINDTUNNEL_SHADERS PREPEND "shaders/")
qt_add_resources(windtunnel "shaders" PREFIX "/" FILES ${WINDTUNNEL_SHADERS})

target_include_directories(windtunnel PRIVATE src ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(windtunnel PRIVATE
  WINDTUNNEL_DEFAULT_MODEL="${WINDTUNNEL_DEFAULT_MODEL}"
)
target_link_libraries(windtunnel PRIVATE
  fluid::core
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Concurrent
  spdlog::spdlog argparse
)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(windtunnel PRIVATE -Wall -Wextra)
endif()

set_target_properties(windtunnel PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
install(TARGETS windtunnel RUNTIME DESTINATION bin BUNDLE DESTINATION .)
