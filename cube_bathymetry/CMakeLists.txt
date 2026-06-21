cmake_minimum_required(VERSION 3.14.4)
project(cube_bathymetry)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(grid_map_ros REQUIRED)
find_package(lifecycle_msgs)
find_package(marine_acoustic_msgs REQUIRED)
find_package(nav_msgs REQUIRED)  # odometry for speed-over-ground
find_package(marine_autonomy REQUIRED)
find_package(geodesy REQUIRED)  # WGS84 Vincenty inverse (geo_grid distance)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

find_package(GDAL CONFIG REQUIRED)

add_library(cube_bathymetry
  src/detections_projector.cpp
  src/error_model.cpp
  src/geo_grid.cpp
  src/geo_map_sheet.cpp
  src/grid.cpp
  src/hypothesis.cpp
  src/map_sheet.cpp
  src/node.cpp
  src/parameters.cpp
)

target_include_directories(cube_bathymetry PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(cube_bathymetry
  ${marine_acoustic_msgs_TARGETS}
  marine_autonomy::marine_autonomy
  # DetectionsProjector needs tf2::BufferCore::lookupTransform + tf2::getEulerYPR
  # (tf2::tf2, the minimal core target -- no rclcpp in its own interface) and
  # tf2::fromMsg(Quaternion) used by getEulerYPR (tf2_geometry_msgs, header-only).
  # The projection *headers/sources* are rclcpp-free, which is what makes the
  # projector unit-testable and reusable offline with only a tf2::BufferCore.
  # Note: the cube_bathymetry .so still links rclcpp transitively (via
  # marine_autonomy, and via tf2_geometry_msgs -> tf2_ros); this is unchanged by
  # the projector and is not a new link-level dependency.
  tf2::tf2
  tf2_geometry_msgs::tf2_geometry_msgs
)
# geodesy (geographic_info) exports via ament, not a namespaced target.
ament_target_dependencies(cube_bathymetry geodesy)

install(TARGETS cube_bathymetry EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})


add_executable(cube_bathymetry_node src/cube_bathymetry_node.cpp)
target_include_directories(cube_bathymetry_node PUBLIC
  ${grid_map_ros_INCLUDE_DIRS}
)
target_link_libraries(cube_bathymetry_node
  cube_bathymetry
  ${geometry_msgs_TARGETS}
  ${grid_map_ros_LIBRARIES}
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
  ${std_srvs_TARGETS}
  tf2_sensor_msgs::tf2_sensor_msgs
)

install(TARGETS cube_bathymetry_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

add_executable(detections_to_pointcloud src/detections_to_pointcloud.cpp)

target_link_libraries(detections_to_pointcloud
  cube_bathymetry
  ${geometry_msgs_TARGETS}
  ${marine_acoustic_msgs_TARGETS}
  ${nav_msgs_TARGETS}
  rclcpp::rclcpp
  rclcpp_lifecycle::rclcpp_lifecycle
  ${sensor_msgs_TARGETS}
  tf2_ros::tf2_ros
)

install(TARGETS detections_to_pointcloud
  DESTINATION lib/${PROJECT_NAME}
)

add_executable(bag_to_geotiff src/bag_to_geotiff.cpp)

target_include_directories(bag_to_geotiff PUBLIC
  ${grid_map_ros_INCLUDE_DIRS}
)

target_link_libraries(bag_to_geotiff
  cube_bathymetry
  GDAL::GDAL
  ${geometry_msgs_TARGETS}
  rclcpp::rclcpp
  rosbag2_cpp::rosbag2_cpp
  rosbag2_transport::rosbag2_transport
  ${sensor_msgs_TARGETS}
  tf2_ros::tf2_ros
)

install(TARGETS bag_to_geotiff
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)

  ament_add_gtest(test_hypothesis test/test_hypothesis.cpp)
  target_link_libraries(test_hypothesis cube_bathymetry)

  ament_add_gtest(test_node test/test_node.cpp)
  target_link_libraries(test_node cube_bathymetry)

  ament_add_gtest(test_parameters test/test_parameters.cpp)
  target_link_libraries(test_parameters cube_bathymetry)

  ament_add_gtest(test_grid test/test_grid.cpp)
  target_link_libraries(test_grid cube_bathymetry)

  ament_add_gtest(test_map_sheet test/test_map_sheet.cpp)
  target_link_libraries(test_map_sheet cube_bathymetry)

  ament_add_gtest(test_geo_map_sheet test/test_geo_map_sheet.cpp)
  target_link_libraries(test_geo_map_sheet cube_bathymetry)

  ament_add_gtest(test_error_model test/test_error_model.cpp)
  target_link_libraries(test_error_model cube_bathymetry)

  ament_add_gtest(test_geo_grid test/test_geo_grid.cpp)
  target_link_libraries(test_geo_grid cube_bathymetry)

  ament_add_gtest(test_detections_projector test/test_detections_projector.cpp)
  target_link_libraries(test_detections_projector cube_bathymetry)
endif()

ament_export_dependencies(rclcpp)

# Export old-style CMake variables
ament_export_include_directories(include/${PROJECT_NAME})
ament_export_libraries(cube_bathymetry)

# Export modern CMake targets
ament_export_targets(export_${PROJECT_NAME})

ament_package()
