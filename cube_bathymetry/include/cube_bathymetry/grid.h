#ifndef CUBE_BATHYMETRY_GRID_H
#define CUBE_BATHYMETRY_GRID_H

#include "node.h"
#include "parameters.h"
#include "sounding.h"
#include "bounds.h"

#include <memory>


namespace cube
{

class Grid
{
public:
  Grid(CellCounts counts, CellSizes sizes, MapPosition origin, const Parameters& parameters);


  /* Routine:	cube_grid_insert_depths
  * Purpose:	Add a sequence of depth estimates to the mapsheet
  * Inputs:	g		CubeGrid structure on which to work
  *			p		Cube algorithm parameters structure
  *			stream	SoundingStream which supplied the data
  *			*data	Pointer to the soundings which should be added
  *			nsnds	Number of soundings in this batch
  *			west	Easting of the most westerly node in the grid
  *			north	Northing of the most northerly node in the grid
  * Outputs:	True if data was added correctly, otherwise False
  *			*n_used set to number of points actually integrated into CubeGrid
  * Comment:	This code integrates the soundings presented into the current
  *			grid.  Note that in the newer version of the algorithm, soundings
  *			are queued in a median pre-filter before going to CUBE so there is
  *			a delay between presentation and readback effect.  Note that we
  *			assume at this point that the soundings presented have suitably
  *			defined error variances in depth and position associated with them,
  *			so that we don't have to try to re-generate them here.
  *				Remember that the grid doesn't have an absolute coordinate
  *			system, which is really a function of the higher order entity which
  *			would typically wrap the raw grid.  However, the soundings are
  *			linked to a particular projection system, and it is unreasonable
  *			for the code calling this to modify to local coordinates, then
  *			call here, then change everything back to global coordinates (since
  *			the caller of *that* routine is providing the soundings, and has a
  *			reasonable right to expect that they aren't going to be munged by
  *			subsidiary calls in the process of being integrated).  Hence, the
  *			caller needs to specify absolute bounds here, and we modify the
  *			node positions from the CubeGrid relative system accordingly.
  */
  bool insert(const Sounding &sounding);
  bool insert(const std::vector<Sounding> & soundings);

  const MapPosition &origin() const;
  const CellCounts &cellCounts() const;
  const CellSizes &cellSizes() const;
  
  MapBounds bounds() const;
  
  std::vector<DepthAndUncertainty> values() const;

private:
  CellCounts counts_;
  CellSizes sizes_;

  MapPosition origin_;

  const Parameters& parameters_;

  std::vector<std::shared_ptr<Node> > nodes_;

};

} // namespace cube

#endif
