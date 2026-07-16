cmake_minimum_required(VERSION 3.14.4)
project(cube_bathymetry)

# This package carries the compute-heavy CUBE estimator + the offline importers
# (import_bag / bag_to_geotiff). colcon supplies no CMAKE_BUILD_TYPE by default,
# which builds at -O0 and makes import_bag ~10-50x slower (an hours-long offline
# import instead of minutes). Default to an optimized build when the workspace
# didn't pick one (cube_bathymetry#63).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Default optimized build" FORCE)
  message(STATUS "cube_bathymetry: defaulting CMAKE_BUILD_TYPE to RelWithDebInfo")
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
find_package(tf2_eigen REQUIRED)
find_package(lifecycle_msgs)
find_package(marine_acoustic_msgs REQUIRED)
find_package(nav_msgs REQUIRED)  # odometry for speed-over-ground
find_package(marine_autonomy REQUIRED)
find_package(marine_bathymetry_store REQUIRED)  # store epoch import (#57)
find_package(marine_interfaces REQUIRED)  # SonarVisualizationTile transport (#230)
find_package(marine_tiled_raster_store REQUIRED)  # TileCatalogBuilder (#230)
find_package(marine_mbes_backscatter_store REQUIRED)  # backscatter store layer (#80)
find_package(rclcpp REQUIRED)
find_package(rclcpp_lifecycle REQUIRED)
find_package(rosbag2_cpp REQUIRED)
find_package(rosbag2_transport REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(tf2_sensor_msgs REQUIRED)

find_package(GDAL CONFIG REQUIRED)

add_library(cube_bathymetry
  src/angular_response_curve.cpp
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

install(TARGETS cube_bathymetry EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})

# GeoGrid -> bathymetry-store-tile conversion (#57, #21). Kept in its OWN target
# so the store dependency does NOT leak into the core cube_bathymetry library
# (and thus not into detections_to_pointcloud, which has no need for the data
# store). Linked by the offline import_bag tool and its test, AND -- since the
# draft-tile persistence work (#21) -- by the runtime cube_bathymetry_node, which
# uses geoGridToTile / loadIntoSheet to save and warm-start draft tiles.
add_library(cube_bathymetry_store_import
  src/store_import.cpp
  src/batch_regen.cpp   # exact scatter/gather store rebuild (#96)
)
target_link_libraries(cube_bathymetry_store_import
  cube_bathymetry
  marine_bathymetry_store::marine_bathymetry_store
  marine_mbes_backscatter_store::marine_mbes_backscatter_store  # MbesCell (#80)
)
install(TARGETS cube_bathymetry_store_import EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)


# GeoMapSheet -> map-frame grid_map projection (#21). In its own target so the
# live publish path is unit-testable (publish-equivalence regression test) with
# only grid_map_core -- no ROS node, no message conversion.
add_library(cube_bathymetry_grid_projection src/grid_projection.cpp)
target_link_libraries(cube_bathymetry_grid_projection
  cube_bathymetry
)
# grid_map_core ships an Eigen-plugin header that must be on the include path and
# its -DEIGEN_*_PLUGIN definitions in scope; ament_target_dependencies wires both
# (plus Eigen3) onto the target, which raw ${grid_map_core_LIBRARIES} does not.
ament_target_dependencies(cube_bathymetry_grid_projection grid_map_core)
install(TARGETS cube_bathymetry_grid_projection EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# GeoGrid -> quantized SonarVisualizationTile (#78, ADR-0008). Own target so the
# marine_interfaces message dependency stays out of the core cube_bathymetry
# library (mirrors grid_projection's isolation) and the quantization is
# unit-testable without a ROS node.
add_library(cube_bathymetry_quantize_tile src/quantize_tile.cpp)
target_link_libraries(cube_bathymetry_quantize_tile
  cube_bathymetry
  ${marine_interfaces_TARGETS}
)
install(TARGETS cube_bathymetry_quantize_tile EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

# SonarInfo -> AngularResponseCurve validation (#102). Own target so the
# marine_interfaces message dependency stays out of the core cube_bathymetry
# library (mirrors quantize_tile's isolation) and the accept/reject logic is
# unit-testable without a ROS node.
add_library(cube_bathymetry_sonar_info_curve src/sonar_info_curve.cpp)
target_link_libraries(cube_bathymetry_sonar_info_curve
  cube_bathymetry
  ${marine_interfaces_TARGETS}
)
install(TARGETS cube_bathymetry_sonar_info_curve EXPORT export_${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

add_executable(cube_bathymetry_node src/cube_bathymetry_node.cpp)
target_include_directories(cube_bathymetry_node PUBLIC
  ${grid_map_ros_INCLUDE_DIRS}
)
target_link_libraries(cube_bathymetry_node
  cube_bathymetry
  cube_bathymetry_grid_projection
  cube_bathymetry_quantize_tile   # GeoGrid -> SonarVisualizationTile (#78)
  cube_bathymetry_sonar_info_curve  # SonarInfo curve delivery (#102)
  cube_bathymetry_store_import   # geoGridToTile / loadIntoSheet + store (#21)
  marine_bathymetry_store::marine_bathymetry_store  # saveTile / load / store API
  marine_tiled_raster_store::marine_tiled_raster_store  # TileCatalogBuilder (#230)
  ${marine_interfaces_TARGETS}  # SonarVisualizationTile / TileCatalog / TileRequest
  ${geometry_msgs_TARGETS}
  ${grid_map_ros_LIBRARIES}
  rclcpp::rclcpp
  ${sensor_msgs_TARGETS}
  tf2_eigen::tf2_eigen
  tf2_geometry_msgs::tf2_geometry_msgs
  tf2_sensor_msgs::tf2_sensor_msgs
)

install(TARGETS cube_bathymetry_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

# Offline empirical angular-response curve derivation tool (cube#81). Reads a
# SonarDetections bag, bins intensities by |rx_angles|, writes the curve CSV the
# Empirical backscatter correction consumes.
install(PROGRAMS
  scripts/derive_angular_response.py
  DESTINATION lib/${PROJECT_NAME}
)

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

# Offline detections-bag -> CUBE GeoMapSheet -> bathymetry-store epoch importer (#57).
add_executable(import_bag src/import_bag_main.cpp)
target_link_libraries(import_bag
  cube_bathymetry_sonar_info_curve  # SonarInfo curve pre-pass (#102)
  cube_bathymetry_store_import   # brings in cube_bathymetry + marine_bathymetry_store
  marine_mbes_backscatter_store::marine_mbes_backscatter_store  # --bs-store output (#80)
  ${geometry_msgs_TARGETS}
  ${marine_acoustic_msgs_TARGETS}
  ${nav_msgs_TARGETS}
  rclcpp::rclcpp
  rosbag2_cpp::rosbag2_cpp
  rosbag2_transport::rosbag2_transport
  tf2_geometry_msgs::tf2_geometry_msgs
  tf2_ros::tf2_ros
)

install(TARGETS import_bag
  DESTINATION lib/${PROJECT_NAME}
)

# Exact, bounded-RAM store rebuild: scatter soundings to per-tile buckets, gather
# each tile in one unbounded pass (no eviction -> bit-exact output) (#96).
add_executable(batch_regen_bag src/batch_regen_main.cpp)
target_link_libraries(batch_regen_bag
  cube_bathymetry_store_import   # BatchRegen + cube_bathymetry + marine_bathymetry_store
  marine_mbes_backscatter_store::marine_mbes_backscatter_store  # --bs-store output (#80)
  ${geometry_msgs_TARGETS}
  ${marine_acoustic_msgs_TARGETS}
  ${nav_msgs_TARGETS}
  rclcpp::rclcpp
  rosbag2_cpp::rosbag2_cpp
  rosbag2_transport::rosbag2_transport
  tf2_geometry_msgs::tf2_geometry_msgs
  tf2_ros::tf2_ros
)

install(TARGETS batch_regen_bag
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

  ament_add_gtest(test_angular_response_curve test/test_angular_response_curve.cpp)
  target_link_libraries(test_angular_response_curve cube_bathymetry)

  ament_add_gtest(test_sonar_info_curve test/test_sonar_info_curve.cpp)
  target_link_libraries(test_sonar_info_curve cube_bathymetry_sonar_info_curve)

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

  ament_add_gtest(test_quantize_tile test/test_quantize_tile.cpp)
  target_link_libraries(test_quantize_tile cube_bathymetry_quantize_tile)

  ament_add_gtest(test_detections_projector test/test_detections_projector.cpp)
  target_link_libraries(test_detections_projector cube_bathymetry)

  ament_add_gtest(test_store_import test/test_store_import.cpp)
  target_link_libraries(test_store_import
    cube_bathymetry_store_import
    marine_mbes_backscatter_store::marine_mbes_backscatter_store  # backscatter tests (#80)
  )

  # Publish-equivalence safety net for the MapSheet->GeoMapSheet migration (#21).
  ament_add_gtest(test_publish_equivalence test/test_publish_equivalence.cpp)
  target_link_libraries(test_publish_equivalence cube_bathymetry_grid_projection)

  # Draft-tile persistence round-trip (#21): save -> store load() -> values match.
  ament_add_gtest(test_persistence test/test_persistence.cpp)
  target_link_libraries(test_persistence
    cube_bathymetry_store_import
    marine_bathymetry_store::marine_bathymetry_store
  )

  # Long-duration bounding (#70): resident tile count stays bounded under a long
  # track while the on-disk store keeps growing (bounded-RSS proxy).
  ament_add_gtest(test_tile_eviction_rss test/test_tile_eviction_rss.cpp)
  target_link_libraries(test_tile_eviction_rss
    cube_bathymetry_store_import
    marine_bathymetry_store::marine_bathymetry_store
  )

  # Bounded == unbounded equivalence for the offline import accumulator (#92):
  # an eviction-forcing budget reproduces the unbounded bathy + backscatter store
  # exactly (lossless persist-then-drop + reload-on-revisit), and a persist
  # failure never drops a tile.
  ament_add_gtest(test_import_eviction test/test_import_eviction.cpp)
  target_link_libraries(test_import_eviction
    cube_bathymetry_store_import
    marine_bathymetry_store::marine_bathymetry_store
    marine_mbes_backscatter_store::marine_mbes_backscatter_store
  )

  # Batch-regen exactness (#96): the scatter/gather rebuild produces a byte-exact
  # bathy + backscatter store vs a single-pass unbounded ImportAccumulator.
  ament_add_gtest(test_batch_regen test/test_batch_regen.cpp)
  target_link_libraries(test_batch_regen
    cube_bathymetry_store_import
    marine_bathymetry_store::marine_bathymetry_store
    marine_mbes_backscatter_store::marine_mbes_backscatter_store
  )
endif()

# The installed public header store_import.h (#include'd by downstream consumers
# of the exported cube_bathymetry_store_import target) pulls in both store
# packages' headers, so re-export them as transitive dependencies — a downstream
# find_package(cube_bathymetry) then resolves their include paths/targets without
# its own find_package. (marine_bathymetry_store was a pre-existing gap here;
# marine_mbes_backscatter_store is new with #80.)
ament_export_dependencies(rclcpp marine_bathymetry_store marine_mbes_backscatter_store)

# Export old-style CMake variables
ament_export_include_directories(include/${PROJECT_NAME})
ament_export_libraries(cube_bathymetry)

# Export modern CMake targets
ament_export_targets(export_${PROJECT_NAME})

ament_package()
