# cube_bathymetry

A ROS implementation of the [Combined Uncertainty and Bathymetric Estimator](https://ccom.unh.edu/theme/data-processing/cube) algrithm, also known as CUBE.

It is based on Brian Calder's original c code found [here](https://bitbucket.org/ccomjhc/cube) as well as Eric Younkin's Python port found [here](https://github.com/noaa-ocs-hydrography/bathycube).

## Development notes

In original code, mapsheet_cube_insert_depths comments points out re-using range member of sounding for interpolated predicted depth and says it will be used by cube_node_insert for slope correction.

mapsheet_cube_insert_depths calls cube_grid_insert_depths
cube_grid_insert_depths call cube_node_insert
cube_node_insert calls cube_node_queue_est
cube_node_queue_est call cube_node_update_node if necessary
