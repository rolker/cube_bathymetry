cmake_minimum_required(VERSION 3.10.2)
project(cube_bathymetry)

set(CMAKE_CXX_STANDARD 14)

find_package(catkin REQUIRED COMPONENTS grid_map_ros
  roscpp rosbag tf2_ros tf2_sensor_msgs
)

find_package(GDAL REQUIRED)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES cube_bathymetry
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${GDAL_INCLUDE_DIR}
)

set(CUBE_LIBRARY_SOURCES
  src/grid.cpp
  src/hypothesis.cpp
  src/map_sheet.cpp
  src/node.cpp
  src/parameters.cpp
)

add_library(cube_bathymetry ${CUBE_LIBRARY_SOURCES})

add_executable(cube_bathymetry_node src/cube_bathymetry_node.cpp)

target_link_libraries(cube_bathymetry_node
  cube_bathymetry
  ${catkin_LIBRARIES}
)

add_executable(bag_to_geotiff src/bag_to_geotiff.cpp)

target_link_libraries(bag_to_geotiff
  cube_bathymetry
  ${catkin_LIBRARIES}
  ${GDAL_LIBRARY}
)

install(TARGETS cube_bathymetry cube_bathymetry_node
    ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
    RUNTIME DESTINATION ${CATKIN_GLOBAL_BIN_DESTINATION}
    )

install(DIRECTORY include/${PROJECT_NAME}/
DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
PATTERN ".svn" EXCLUDE
)
