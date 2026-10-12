cmake_minimum_required(VERSION 3.14.4)
project(coverage_assessment)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(grid_map_core REQUIRED)

# The core (polygon rasterising, per-source accumulation, judging) is plain
# C++ over grid_map_core: no ROS, no message types, no TF. The ROS wrapper
# (a later step of #164) converts messages and frames into the core's own
# structs, which keeps every judging rule unit-testable without a graph.
add_library(coverage_assessment_core
  src/polygon_raster.cpp
)
target_include_directories(coverage_assessment_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}>
)
# grid_map_core ships an Eigen-plugin header that must be on the include path and
# its -DEIGEN_*_PLUGIN definitions in scope; ament_target_dependencies wires both
# onto the target, which raw ${grid_map_core_LIBRARIES} does not.
ament_target_dependencies(coverage_assessment_core grid_map_core)
install(TARGETS coverage_assessment_core EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_polygon_raster test/test_polygon_raster.cpp)
  target_link_libraries(test_polygon_raster coverage_assessment_core)
endif()

ament_package()
