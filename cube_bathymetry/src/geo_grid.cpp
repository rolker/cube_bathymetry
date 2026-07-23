// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#include "cube_bathymetry/geo_grid.h"
#include <cmath>

namespace cube
{

GeoGrid::GeoGrid(gggs::GridIndex index, const Parameters & parameters)
:index_(index), parameters_(parameters)
{
}

bool GeoGrid::insert(const std::vector<GeoSounding> & soundings)
{
  bool ret = false;
  for (const auto & s  :  soundings) {
    ret = insert(s) || ret;
  }
  return ret;
}


bool GeoGrid::insert(const GeoSounding & geo_sounding)
{
  const Sounding & sounding = geo_sounding.sounding;
  // Reject non-finite/degenerate soundings at the door (parity with the planar
  // Grid::insert gate): a NaN position/depth or a non-positive vertical error
  // propagates through the CUBE variance math into NaN hypothesis estimates
  // that can blank cells, and horizontal_error feeds sqrt() in influenceRadius,
  // where a negative value (not just NaN) reintroduces NaN. One bad sounding
  // must never be able to empty the grid.
  if(!std::isfinite(geo_sounding.latitude) || !std::isfinite(geo_sounding.longitude) ||
    !std::isfinite(sounding.depth) ||
    !std::isfinite(sounding.vertical_error) ||
    !std::isfinite(sounding.horizontal_error) ||
    sounding.vertical_error <= 0.0 ||
    sounding.horizontal_error < 0.0)
  {
    return false;
  }

  // Shared with Grid::insert and GeoMapSheet's grid-selection margin so the
  // spread region and the selected-tile set can never drift apart (#104).
  const double radius = parameters_.influenceRadius(sounding);
  // Defensive: with door-gated inputs the radius is finite (overflow clamps to
  // max_radius), but a non-finite radius must never reach radiusFromCenter's
  // corners and the cell iterator.
  if(!std::isfinite(radius)) {
    return false;
  }

  // Local-planar (equirectangular) cell->sounding distance in metres. This runs
  // per cell x per sounding x per ping; the #144 gz4d->GeoPoint refactor had put
  // a WGS84 Vincenty inverse here -- a full iterative ellipsoidal solver -- for
  // what is a sub-metre cell-to-sounding distance. At these radii (a few metres)
  // the equirectangular distance matches the geodesic to well under a millimetre,
  // so the iterative solver was pure overhead in both the offline import and the
  // live node (cube_bathymetry#63). Latitude scale is fixed at the sounding (the
  // cells span only metres around it).
  static constexpr double kDeg2Rad = M_PI / 180.0;
  static constexpr double kRad2Deg = 180.0 / M_PI;
  static constexpr double kEarthRadiusM = 6378137.0;  // WGS84 semi-major axis
  const double cos_lat = std::cos(geo_sounding.latitude * kDeg2Rad);

  // Search box derived with the SAME equirectangular metric as the in-loop gate
  // below, replacing gz4d::BoundsDegrees::radiusFromCenter -- an ellipsoidal
  // tan()/reduced-latitude solve that was itself a profiler sample (cube#107).
  // INVARIANT that keeps the change bit-exact: the box must be a SUPERSET of the
  // gate region. A cell passes the gate only when hypot(dlat_m, dlon_m) < radius,
  // which forces |dlat_m| < radius AND |dlon_m| < radius; dividing by the gate's
  // own metres-per-degree (kEarthRadiusM*kDeg2Rad in latitude, times cos_lat in
  // longitude) yields exactly the half-widths below. So every cell the gate would
  // accept lies inside the box -- the box never clips a kept cell.
  const double delta_lat_deg = radius / kEarthRadiusM * kRad2Deg;
  const double delta_lon_deg = radius / (kEarthRadiusM * cos_lat) * kRad2Deg;
  gggs::CellAreaIterator i(index_,
    gggs::geoPoint(geo_sounding.latitude - delta_lat_deg,
                   geo_sounding.longitude - delta_lon_deg),
    gggs::geoPoint(geo_sounding.latitude + delta_lat_deg,
                   geo_sounding.longitude + delta_lon_deg));

  bool inserted = false;
  while(i.valid()) {
    const auto cell = i->position();
    const double dlat = (cell.latitude - geo_sounding.latitude) * kDeg2Rad;
    const double dlon = (cell.longitude - geo_sounding.longitude) * kDeg2Rad * cos_lat;
    const double distance = std::hypot(dlat, dlon) * kEarthRadiusM;
    if(distance < radius) {
      // Single hash-map probe (was three separate map descents per hit cell,
      // each re-running the fat gggs::operator< -- cube#107).
      auto & node = nodes_[nodeKey(*i)];
      if(!node) {
        node = std::make_shared<Node>();
      }
      inserted = node->insert(distance, sounding, parameters_) || inserted;
    }
    i.next();
  }

  return inserted;
}

void GeoGrid::setPredictedDepthAt(
  const gggs::CellIndex & cell, float depth, float variance)
{
  // Lazy-create the Node so a warm-start prime can seed a cell that the live
  // session has not yet touched. Mirrors insert()'s node creation.
  auto & node = nodes_[nodeKey(cell)];
  if(!node) {
    node = std::make_shared<Node>();
  }
  node->setPredictedDepth(depth, variance);
}

void GeoGrid::setSettledDepthAt(
  const gggs::CellIndex & cell, float depth, float uncertainty)
{
  // Lazy-create the Node (mirrors setPredictedDepthAt) so the reload can seed a
  // cell the live session has not yet touched this run.
  auto & node = nodes_[nodeKey(cell)];
  if(!node) {
    node = std::make_shared<Node>();
  }
  node->seedSettledDepth(depth, uncertainty, parameters_);
}

float GeoGrid::predictedDepthAt(const gggs::CellIndex & cell) const
{
  auto it = nodes_.find(nodeKey(cell));
  if(it == nodes_.end() || !it->second) {
    return INVALID_DATA;
  }
  return it->second->predictedDepth();
}

const gggs::GridIndex & GeoGrid::index() const
{
  return index_;
}

std::vector<DepthAndUncertainty> GeoGrid::values() const
{
  std::vector<DepthAndUncertainty> ret;

  gggs::CellAreaIterator i(index_);

  while(i.valid()) {
    auto node = nodes_.find(nodeKey(*i));

    if(node == nodes_.end() || !node->second) {
      // empty node, so default nan value
      ret.push_back(DepthAndUncertainty());
    } else {
      node->second->queueFlush(parameters_);
      ret.push_back(node->second->extractDepthAndUncertainty(parameters_));
    }
    i.next();
  }
  return ret;
}

std::vector<NodeRecord> GeoGrid::nodeRecords() const
{
  std::vector<NodeRecord> ret;

  gggs::CellAreaIterator i(index_);

  while(i.valid()) {
    auto node = nodes_.find(nodeKey(*i));

    if(node == nodes_.end() || !node->second) {
      // empty node, so default record (NaN depth + NaN intensity)
      ret.push_back(NodeRecord());
    } else {
      node->second->queueFlush(parameters_);
      ret.push_back(node->second->extractNodeRecord(parameters_));
    }
    i.next();
  }
  return ret;
}

std::map<gggs::CellIndex, IntensityWelford>
GeoGrid::nodeIntensityWelford() const
{
  std::map<gggs::CellIndex, IntensityWelford> ret;
  // Iterate the SPARSE node map (only created cells), not a CellAreaIterator over
  // all 960x960 cells: a tile has far fewer surveyed nodes than cells, so this is
  // both faster and avoids visiting ~900k empty cells.
  for (const auto & entry : nodes_) {
    if(!entry.second) {
      continue;
    }
    entry.second->queueFlush(parameters_);
    const IntensityWelford w = entry.second->chosenIntensityWelford();
    if(w.n > 0) {
      // Reconstruct the CellIndex from the packed uint32 node key (cube#107):
      // all nodes in this grid share index_, and the key packs (row<<16)|column.
      const gggs::CellIndex cell(index_,
        static_cast<uint16_t>(entry.first >> 16),
        static_cast<uint16_t>(entry.first & 0xFFFF));
      ret.emplace(cell, w);
    }
  }
  return ret;
}

void GeoGrid::setSettledIntensityWelfordAt(
  const gggs::CellIndex & cell, const IntensityWelford & intensity)
{
  auto it = nodes_.find(nodeKey(cell));
  if(it == nodes_.end() || !it->second) {
    return;  // no node here (depth reload did not seed this cell) -- nothing to do
  }
  it->second->setSettledIntensityWelford(intensity);
}

}  // namespace cube
