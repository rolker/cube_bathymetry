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
#include "marine_autonomy/gz4d_geo.h"

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
  double max_variance_allowed = parameters_.maxVarianceAllowed(sounding.depth);
  double ratio = max_variance_allowed / sounding.vertical_error;

  /* Ensure some spreading on point */
  if(ratio <= 2.0) {
    ratio = 2.0;
  }

  double max_radius = CONF_99PC * std::sqrt(sounding.horizontal_error);

  double radius = parameters_.distance_scale * pow(ratio - 1.0,
      parameters_.inverse_distance_exponent) - max_radius;
  if (radius < 0.0) {
    radius = parameters_.distance_scale;
  }
  if (radius > max_radius) {
    radius = max_radius;
  }
  if (radius < parameters_.distance_scale) {
    radius = parameters_.distance_scale;
  }


  auto bounds = gz4d::BoundsDegrees::radiusFromCenter(geo_sounding, radius);

  // The GGGS CellAreaIterator now takes geographic_msgs GeoPoint corners
  // (gz4d retired from the GGGS API, unh_marine_autonomy#144). cube keeps
  // gz4d internally for radiusFromCenter and converts the corners here.
  gggs::CellAreaIterator i(index_,
    gggs::geoPoint(bounds.minimum().latitude, bounds.minimum().longitude),
    gggs::geoPoint(bounds.maximum().latitude, bounds.maximum().longitude));

  // Local-planar (equirectangular) cell->sounding distance in metres. This runs
  // per cell x per sounding x per ping; the #144 gz4d->GeoPoint refactor had put
  // a WGS84 Vincenty inverse here -- a full iterative ellipsoidal solver -- for
  // what is a sub-metre cell-to-sounding distance. At these radii (a few metres)
  // the equirectangular distance matches the geodesic to well under a millimetre,
  // so the iterative solver was pure overhead in both the offline import and the
  // live node (cube_bathymetry#63). Latitude scale is fixed at the sounding (the
  // cells span only metres around it).
  static constexpr double kDeg2Rad = M_PI / 180.0;
  static constexpr double kEarthRadiusM = 6378137.0;  // WGS84 semi-major axis
  const double cos_lat = std::cos(geo_sounding.latitude * kDeg2Rad);

  bool inserted = false;
  while(i.valid()) {
    const auto cell = i->position();
    const double dlat = (cell.latitude - geo_sounding.latitude) * kDeg2Rad;
    const double dlon = (cell.longitude - geo_sounding.longitude) * kDeg2Rad * cos_lat;
    const double distance = std::hypot(dlat, dlon) * kEarthRadiusM;
    if(distance < radius) {
      if(!nodes_[*i]) {
        nodes_[*i] = std::make_shared<Node>();
      }
      inserted = nodes_[*i]->insert(distance, sounding, parameters_) || inserted;
    }
    i.next();
  }

  return inserted;
}

void GeoGrid::setPredictedDepthAt(
  const gggs::CellIndex & cell, float depth, float variance)
{
  // Lazy-create the Node so a warm-start prime can seed a cell that the live
  // session has not yet touched. Mirrors insert()'s nodes_[*i] creation.
  if(!nodes_[cell]) {
    nodes_[cell] = std::make_shared<Node>();
  }
  nodes_[cell]->setPredictedDepth(depth, variance);
}

void GeoGrid::setSettledDepthAt(
  const gggs::CellIndex & cell, float depth, float uncertainty)
{
  // Lazy-create the Node (mirrors setPredictedDepthAt) so the reload can seed a
  // cell the live session has not yet touched this run.
  if(!nodes_[cell]) {
    nodes_[cell] = std::make_shared<Node>();
  }
  nodes_[cell]->seedSettledDepth(depth, uncertainty, parameters_);
}

float GeoGrid::predictedDepthAt(const gggs::CellIndex & cell) const
{
  auto it = nodes_.find(cell);
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
    auto node = nodes_.find(*i);

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
    auto node = nodes_.find(*i);

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

}  // namespace cube
