
include_directories(
  .
  ccom_base
  ccom_core
  cube
  errmod
  mapsheet
  omg
  sounding
)

add_subdirectory(ccom_base)
add_subdirectory(ccom_core)
add_subdirectory(cube)
add_subdirectory(errmod)
add_subdirectory(gctpc/source)
add_subdirectory(mapsheet)
