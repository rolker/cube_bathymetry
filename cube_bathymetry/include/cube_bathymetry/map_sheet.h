#ifndef CUBE_BATHYMETRY_MAP_SHEET_H
#define CUBE_BATHYMETRY_MAP_SHEET_H

#include "grid.h"
#include <map>
#include <chrono>

namespace cube
{

/// A grid of Grids used to grow surfaces without knowing the bounds
/// ahead of time.
class MapSheet
{
public:
  /// Constructor where counts is number of cells in individual grids, sizes contains the size of
  /// individual cells and order is the IHO order.
  MapSheet(CellCounts counts, CellSizes sizes, std::string iho_order = "order1a");

  void addSoundings(const std::vector<Sounding> & soundings, std::chrono::steady_clock::time_point time = std::chrono::steady_clock::now());

  /// Return the grids within the bounds, creating new ones if necessary
  std::vector<std::shared_ptr<Grid> > getOrCreateGridsIn(const MapBounds& bounds);

  /// Return all existing grids
  std::vector<std::shared_ptr<Grid> > grids() const;

  /// Return total cell count of rectangle containing all the grids
  CellCounts totalCellCounts() const;

  /// Return bounds in map coordinates of rectangle containing all the grids
  MapBounds gridBounds() const;

  const CellSizes& cellSizes() const;
  const CellCounts& cellCountsPerGrid() const;

  GridIndex gridIndex(const MapPosition &position) const;

  std::chrono::steady_clock::time_point lastUpdateTime() const;

private:
  /// Grid cell counts
  CellCounts counts_;
  /// Cell sizes (meters)
  CellSizes sizes_;

  Parameters parameters_;

  std::map<GridIndex, std::shared_ptr<Grid> > grids_;

  std::chrono::steady_clock::time_point last_update_time_;
};

} // namespace cube

#endif
