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
find_package(project11 REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

find_package(GDAL REQUIRED)

add_library(cube_bathymetry
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
  project11::project11
)

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
  tf2_sensor_msgs::tf2_sensor_msgs
)

install(TARGETS cube_bathymetry_node
  DESTINATION lib/${PROJECT_NAME}
)


# add_executable(bag_to_geotiff src/bag_to_geotiff.cpp)

# target_link_libraries(bag_to_geotiff
#   cube_bathymetry
#   ${GDAL_LIBRARY}
# )

# install(TARGETS bag_to_geotiff
#   DESTINATION lib/${PROJECT_NAME}
# )

ament_export_dependencies(rclcpp)

# Export old-style CMake variables
ament_export_include_directories(include/${PROJECT_NAME})
ament_export_libraries(cube_bathymetry)

# Export modern CMake targets
ament_export_targets(export_${PROJECT_NAME})

ament_package()
