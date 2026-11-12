// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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


#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/node.h"
#include "cube_bathymetry/parameters.h"
#include "cube_bathymetry/sounding.h"

/// @file
/// @brief The backscatter angle basis (cube_bathymetry#173): which angle of a
///        sounding the angular-response correction is indexed by.

namespace cube
{
namespace
{
float deg2rad(float deg) {return deg * static_cast<float>(M_PI) / 180.0f;}

// Empirical curve: 0 dB at nadir, -12 dB at 60 deg (so -0.2 dB per degree).
Parameters curveParams(BackscatterAngleBasis basis)
{
  Parameters p{CellSizes(1.0f), "order1a"};
  p.backscatter_angle_correction = BackscatterAngleCorrection::Empirical;
  p.angular_response_curve = {{0.0f, 0.0f}, {60.0f, -12.0f}};
  p.backscatter_angle_basis = basis;
  return p;
}

// One well-formed beam; its beam angle is nadir so only the basis can move the
// correction.
Sounding beam(float raw_intensity, float incidence_rad)
{
  Sounding s(10.0f);
  s.intensity = raw_intensity;
  s.beam_angle = 0.0f;
  s.incidence_angle = incidence_rad;
  s.vertical_error = 0.01f;
  s.horizontal_error = 0.01f;
  return s;
}

// The corrected intensity of exactly one beam, through the node's own door.
float correctedMean(const Parameters & p, const Sounding & s)
{
  Node n;
  n.insert(0.0, s, p);
  n.queueFlush(p);
  const auto record = n.extractNodeRecord(p);
  return record.n_samples == 1u ? record.intensity : std::nanf("");
}
}  // namespace

TEST(AngleBasis, ParseAndPrint)
{
  BackscatterAngleBasis basis = BackscatterAngleBasis::Beam;
  EXPECT_TRUE(parseBackscatterAngleBasis("incidence", basis));
  EXPECT_EQ(basis, BackscatterAngleBasis::Incidence);
  EXPECT_TRUE(parseBackscatterAngleBasis("BEAM", basis));
  EXPECT_EQ(basis, BackscatterAngleBasis::Beam);
  EXPECT_STREQ(toString(BackscatterAngleBasis::Incidence), "incidence");
  EXPECT_STREQ(toString(BackscatterAngleBasis::Beam), "beam");
  // No legacy empty spelling: an unset option must be handled by the caller.
  basis = BackscatterAngleBasis::Incidence;
  EXPECT_FALSE(parseBackscatterAngleBasis("", basis));
  EXPECT_FALSE(parseBackscatterAngleBasis("grazing", basis));
  EXPECT_EQ(basis, BackscatterAngleBasis::Incidence) << "a refused value leaves out untouched";
}

TEST(AngleBasis, DefaultIsBeamOnSheetAndParameters)
{
  EXPECT_EQ(Parameters(CellSizes(1.0f)).backscatter_angle_basis, BackscatterAngleBasis::Beam);
  GeoMapSheet sheet(1.0f, "order1a");
  EXPECT_EQ(sheet.parameters().backscatter_angle_basis, BackscatterAngleBasis::Beam);
  sheet.setBackscatterAngleBasis(BackscatterAngleBasis::Incidence);
  EXPECT_EQ(sheet.parameters().backscatter_angle_basis, BackscatterAngleBasis::Incidence);
}

TEST(AngleBasis, CorrectionAngleSelectsTheBasis)
{
  Sounding s = beam(-30.0f, deg2rad(25.0f));
  s.beam_angle = deg2rad(5.0f);
  EXPECT_FLOAT_EQ(curveParams(BackscatterAngleBasis::Beam).correctionAngle(s), deg2rad(5.0f));
  EXPECT_FLOAT_EQ(
    curveParams(BackscatterAngleBasis::Incidence).correctionAngle(s), deg2rad(25.0f));
}

// The incidence basis corrects by the incidence angle: a beam whose raw
// intensity is A + f(incidence) comes out at A for every incidence, whatever
// its (here constant) sonar-frame beam angle.
TEST(AngleBasis, IncidenceBasisRemovesTheIncidenceDependence)
{
  const float a = -30.0f;
  const Parameters p = curveParams(BackscatterAngleBasis::Incidence);
  for (float inc_deg : {0.0f, 10.0f, 25.0f, 40.0f, 55.0f}) {
    const float raw = a + (-0.2f * inc_deg);
    EXPECT_NEAR(correctedMean(p, beam(raw, deg2rad(inc_deg))), a, 2e-4) << inc_deg << " deg";
  }
}

// Negative control: the same beams on the beam basis are corrected by the
// nadir beam angle, so the incidence dependence stays in. If the switch were a
// no-op the incidence test above could not tell.
TEST(AngleBasis, BeamBasisIgnoresIncidence)
{
  const float a = -30.0f;
  const Parameters p = curveParams(BackscatterAngleBasis::Beam);
  for (float inc_deg : {10.0f, 25.0f, 55.0f}) {
    const float raw = a + (-0.2f * inc_deg);
    EXPECT_NEAR(correctedMean(p, beam(raw, deg2rad(inc_deg))), raw, 2e-4) << inc_deg << " deg";
  }
}

// A beam with no incidence angle has nothing for the curve to index: it is
// recorded without the angle term (as a NaN beam angle always was).
TEST(AngleBasis, NanIncidenceSkipsTheAngleTerm)
{
  const Parameters p = curveParams(BackscatterAngleBasis::Incidence);
  EXPECT_NEAR(correctedMean(p, beam(-33.0f, std::nanf(""))), -33.0f, 1e-4);
}

}  // namespace cube
