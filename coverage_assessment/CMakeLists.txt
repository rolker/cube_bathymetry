cmake_minimum_required(VERSION 3.14.4)
project(coverage_assessment)

# colcon supplies no CMAKE_BUILD_TYPE by default, which builds at -O0: one
# assessment pass over 1e6 cells x 4 sources took 0.9 s there against 0.1 s at
# -O2. Default to an optimized build when the workspace didn't pick one (same
# block as cube_bathymetry's).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Default optimized build" FORCE)
  message(STATUS "coverage_assessment: defaulting CMAKE_BUILD_TYPE to RelWithDebInfo")
endif()

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

  # The node, end to end over TF and synthetic grids (B8). Its own domain id and
  # localhost-only discovery keep it from live boats and from parallel colcon tests
  # (ROS_AUTOMATIC_DISCOVERY_RANGE replaces ROS_LOCALHOST_ONLY on jazzy; the id must
  # be <= 101; cube_bathymetry's tests use 87, mru_transform 89, marine_web_view 101).
  # Any discovery-server / static-peer setting inherited from the environment is
  # blanked, as in cube_bathymetry's CUBE_ISOLATED_TEST_ENV.
  set(COVERAGE_ISOLATED_TEST_ENV
    "ROS_DOMAIN_ID=86;ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST"
    "ROS_DISCOVERY_SERVER=;ROS_STATIC_PEERS=;FASTRTPS_DEFAULT_PROFILES_FILE=;CYCLONEDDS_URI=")
  ament_add_gtest(test_coverage_assessment_node test/test_coverage_assessment_node.cpp
    TIMEOUT 300)
  target_include_directories(test_coverage_assessment_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(test_coverage_assessment_node coverage_assessment_node_link)
  set_tests_properties(test_coverage_assessment_node PROPERTIES
    ENVIRONMENT "${COVERAGE_ISOLATED_TEST_ENV}")

  # The launch file's activation handler: activate once after the first
  # configure, and leave a later reset sequence alone (reset_cube_nodes.sh
  # --consumer: deactivate, cleanup, configure, activate).
  find_package(launch_testing_ament_cmake REQUIRED)
  add_launch_test(test/test_launch_lifecycle.py
    TIMEOUT 120
    ENV "ROS_DOMAIN_ID=85" "ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST"
    "ROS_DISCOVERY_SERVER=" "ROS_STATIC_PEERS=" "FASTRTPS_DEFAULT_PROFILES_FILE="
    "CYCLONEDDS_URI=")

  # The polygon and the grids must share one projection chain: pin the node's
  # vertex projection against cube_bathymetry's own (B8 (i)). Needs the built
  # cube_bathymetry package, which colcon builds first (test_depend).
  find_package(cube_bathymetry QUIET)
  if(cube_bathymetry_FOUND)
    ament_add_gtest(test_frame_chain test/test_frame_chain.cpp)
    # cube_bathymetry installs its headers under include/cube_bathymetry/ and
    # exports that directory through cube_bathymetry_INCLUDE_DIRS.
    target_include_directories(test_frame_chain PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${cube_bathymetry_INCLUDE_DIRS})
    target_link_libraries(test_frame_chain
      coverage_assessment_node_link
      cube_bathymetry::cube_bathymetry_grid_projection)
  else()
    # Never skip silently: a missing cube_bathymetry is a failing test, not an
    # absent one. Build both: `colcon build --packages-up-to coverage_assessment`.
    message(WARNING "cube_bathymetry not found: test_frame_chain will FAIL. "
      "Build cube_bathymetry first (colcon --packages-up-to coverage_assessment).")
    add_test(NAME test_frame_chain COMMAND ${CMAKE_COMMAND} -E false)
  endif()
endif()

ament_package()
