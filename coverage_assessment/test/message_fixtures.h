// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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

// Message builders shared by the tests: grid_map messages with an `elevation`
// and an `uncertainty` layer, filled from a function of the cell centre.

#ifndef MESSAGE_FIXTURES_H_
#define MESSAGE_FIXTURES_H_

#include <limits>

#include "grid_map_core/grid_map_core.hpp"

namespace coverage_assessment
{

  const float kNan = std::numeric_limits < float > ::quiet_NaN();

// A message of `nx` x `ny` cells of `res` metres whose lower-left corner is at
// (x0, y0) in the message's own frame. `fill(cx, cy, &elevation, &uncertainty)`
// is called with each cell's centre; it leaves NaN where it does not assign.
  template < typename F >
  grid_map::GridMap makeMessage(double x0, double y0, int nx, int ny, double res, F fill)
  {
    grid_map::GridMap m({"elevation", "uncertainty"});
    m.setFrameId("src");
    m.setGeometry(
    grid_map::Length(nx * res, ny * res), res,
    grid_map::Position(x0 + nx * res / 2.0, y0 + ny * res / 2.0));
    for (grid_map::GridMapIterator it(m); !it.isPastEnd(); ++it) {
      grid_map::Position p;
      m.getPosition(*it, p);
      float e = kNan, u = kNan;
      fill(p.x(), p.y(), &e, &u);
      m.at("elevation", *it) = e;
      m.at("uncertainty", *it) = u;
    }
    return m;
  }

  inline grid_map::GridMap constantMessage(
    double x0, double y0, int nx, int ny, float elevation, float uncertainty)
  {
    return makeMessage(
    x0, y0, nx, ny, 1.0, [ = ](double, double, float * e, float * u) {
      *e = elevation;
      *u = uncertainty;
    });
  }

}  // namespace coverage_assessment

#endif  // MESSAGE_FIXTURES_H_
