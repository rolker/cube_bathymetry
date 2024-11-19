#ifndef CUBE_BATHYMETRY_GEO_MAP_SHEET_H
#define CUBE_BATHYMETRY_GEO_MAP_SHEET_H

#include "geo_grid.h"
#include <map>
#include <chrono>

#include "project11/gggs.h"

namespace cube
{

/// Uses GlobalGGS grid heirarchy to organize Grids.
class GeoMapSheet
{
public:
  /// Constructor where cell_size is approximate resolution requested.
  GeoMapSheet(float cell_size, std::string iho_order = "order1a");

  void addSoundings(const std::vector<GeoSounding> & soundings, std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// Return the grids within the bounds, creating new ones if necessary
  std::vector<std::shared_ptr<GeoGrid> > getOrCreateGridsIn(const gz4d::BoundsDegrees& bounds);

  /// Return all existing grids
  std::vector<std::shared_ptr<GeoGrid> > grids() const;

  /// Return gggs::GridIndex bounds of rectangle containing all the grids
  gggs::GridBounds gridBounds() const;

  /// Cell size in degrees
  double cellSizeDegrees() const;

  double nominalCellSizeMeters() const;

  std::chrono::steady_clock::time_point lastUpdateTime() const;

private:
  /// Grid cell counts
  //CellCounts counts_;
  /// Cell sizes (meters)
  //CellSizes sizes_;

  Parameters parameters_;

  gggs::Level grid_level_;

  std::map<gggs::GridIndex, std::shared_ptr<GeoGrid> > grids_;

  std::chrono::steady_clock::time_point last_update_time_;
};

} // namespace cube

#endif
