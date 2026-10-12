cmake_minimum_required(VERSION 3.14.4)
project(coverage_assessment)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(Eigen3 REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(grid_map_core REQUIRED)
find_package(grid_map_ros REQUIRED)
find_package(grid_map_msgs REQUIRED)
find_package(marine_autonomy REQUIRED)
find_package(marine_interfaces REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_eigen REQUIRED)
find_package(tf2_ros REQUIRED)

# The core (polygon rasterising, per-source accumulation, judging) is plain
# C++ over grid_map_core: no ROS, no message types, no TF. The ROS wrapper
# (src/coverage_assessment_node.hpp) converts messages and frames into the
# core's own structs, which keeps every judging rule unit-testable without a
# graph.
add_library(coverage_assessment_core
  src/polygon_raster.cpp
  src/source_accumulator.cpp
  src/area_registry.cpp
  src/coverage_assessor.cpp
  src/assessment_raster.cpp
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

# The node's compile/link requirements live on an INTERFACE target so the
# executable and the node tests consume ONE list and cannot drift. The node class
# is src/coverage_assessment_node.hpp -- deliberately not under include/, so it is
# neither installed nor public API; consumers add src/ to their own private
# include path.
add_library(coverage_assessment_node_link INTERFACE)
target_include_directories(coverage_assessment_node_link INTERFACE
  ${grid_map_ros_INCLUDE_DIRS}
)
target_link_libraries(coverage_assessment_node_link INTERFACE
  coverage_assessment_core
  marine_autonomy::marine_autonomy  # gz4d geodesy, shared with cube_bathymetry's projection
  Eigen3::Eigen
  ${geometry_msgs_TARGETS}
  ${marine_interfaces_TARGETS}
  ${grid_map_msgs_TARGETS}
  ${grid_map_ros_LIBRARIES}
  rclcpp::rclcpp
  rclcpp_lifecycle::rclcpp_lifecycle
  tf2::tf2
  tf2_eigen::tf2_eigen
  tf2_ros::tf2_ros
)

add_executable(coverage_assessment_node src/coverage_assessment_node.cpp)
target_include_directories(coverage_assessment_node PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(coverage_assessment_node coverage_assessment_node_link)
install(TARGETS coverage_assessment_node DESTINATION lib/${PROJECT_NAME})

install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_polygon_raster test/test_polygon_raster.cpp)
  target_link_libraries(test_polygon_raster coverage_assessment_core)

  ament_add_gtest(test_source_accumulator test/test_source_accumulator.cpp)
  target_link_libraries(test_source_accumulator coverage_assessment_core)

  ament_add_gtest(test_coverage_assessor test/test_coverage_assessor.cpp)
  target_link_libraries(test_coverage_assessor coverage_assessment_core)

  ament_add_gtest(test_assessment_raster test/test_assessment_raster.cpp)
  target_link_libraries(test_assessment_raster coverage_assessment_core)
endif()

ament_package()
