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


#ifndef CUBE_BATHYMETRY__PARAMETERS_H_
#define CUBE_BATHYMETRY__PARAMETERS_H_

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include "cube_bathymetry/common.h"

namespace cube
{

/// Per-beam backscatter angular-response correction applied at node-output
/// (Node::extractNodeRecord, ADR-0007 D3). Default None preserves the Phase B
/// identity behavior (corrected == raw) on every existing deployment.
  enum class BackscatterAngleCorrection
  {
  /// No correction: the surfaced intensity is the raw per-beam mean.
    None,

  /// Empirical angular-response (ARA): subtract a per-sonar curve's
  /// db_relative_to_nadir, linearly interpolated by |beam_angle| in degrees.
  /// Requires a non-empty angular_response_curve; empty curve -> no-op.
    Empirical
  };

/// Extraction method for depth and uncertainty surfaces.  This is used only in
/// the multiple hypothesis case as a way of choosing which hypothesis to
/// extract and report as the `current best guess'.
  enum  CubeExtractor
  {
  /// The CUBE_PRIOR is based on approximate hypothesis probability as estimated
  /// by the number of samples incorporated
    CUBE_PRIOR = 0,

  /// CUBE_LHOOD uses a local spatial context to choose a guide
  /// estimation node (i.e., the closest one with only one hypothesis) and then
  /// chooses the hypothesis at the current node using a minimum distance metric;
    CUBE_LHOOD,

  /// CUBE_POSTERIOR combines both of these to form an approximate Bayesian
  /// posterior distribution.
    CUBE_POSTERIOR,

  /// CUBE_PREDSURF uses the predicted surface depth and
  /// variance to guide the disambiguation process.
    CUBE_PREDSURF,

  /// CUBE_UNKN is a sentinel.
    CUBE_UNKN
  };

  class MapSheet;

/// Algorithm control parameters
  struct Parameters
  {
    static constexpr float DEFAULT_MIN_CONTEXT = 5.0; /* Minimum context distance, m */
    static constexpr float DEFAULT_MAX_CONTEXT = 10.0; /* Maximum context distance, m */


    explicit Parameters(CellSizes sizes, std::string iho_order = "order1a");
    void setIHOLimits(std::string order);
    void setGridResolution(CellSizes sizes);

  /// Maximum allowable propagation variance for a sounding at the given depth:
  /// (iho_fixed + iho_percent*depth^2) / CONF_95PC^2.  The *whole* IHO numerator
  /// is divided by CONF_95PC^2 (Calder, cube_grid.c:1909-1911).  Kept in one place
  /// so the operator-precedence trap that dropped iho_fixed out of the division
  /// (#46) cannot recur in the two grid call sites.
    double maxVarianceAllowed(double depth) const;

  /// Value used to indicate 'no data' (typ. FLT_MAX)
    float no_data_value = std::numeric_limits < float > ::quiet_NaN();

  /// Method used to extract information from sheet
    CubeExtractor extractor = CUBE_LHOOD;

  /// Depth to initialise estimates
    double nodata_depth = 0.0;

  /// Variance for initialisation
    double nodata_variance = 1.0e6;

  /// Exponent on distance for variance scale
    double distance_exponent = 2.0;

  /// 1.0/distance_exponent for efficiency
    double inverse_distance_exponent = 1.0 / distance_exponent;

  /// Normalisation coefficient for distance (m)
    double distance_scale = 0.0;

  /// Variance scale dilution factor
    double variance_scale = 0.0;


  /// IHO order label
    std::string iho_order;

  /// Fixed portion of IHO error budget (m^2)
    double iho_fixed;

  /// Variable portion of IHO error budget (unitless)
    double iho_percent;

  /// Length of median pre-filter sort queue
    uint32_t median_length = 11;

  /// Outlier quotient upper allowable limit
    float quotient_limit = 30.0;

  /// Discount factor for evolution noise variance
    float discount = 1.0;

  /// Threshold for significant offset from current
  /// estimate to warrant an intervention
    float estimate_offset = 4.0;

  /// Bayes factor threshold for either a single
  /// estimate, or the worst-case recent sequence to
  /// warrant an intervention
    float bayes_factor_threshold = 0.135;

  /// Run-length threshold for worst-case recent
  /// sequence to indicate a drift failure and hence
  /// to warrant an intervention
    uint32_t runlength_threshold = 5;

  /// Minimum context search range for hypothesis
  /// disambiguation algorithm
    float minimum_context_search_range = 5.0;

  /// Maximum context search range for hypothesis
  /// disambiguation algorithm
    float maximum_context_search_range = 10.0;

  /// Scale from Std. Dev. to CI
    float stddev_to_confidence_interval_scale = 1.96;

  /// Minimum depth difference from pred. depth to
  /// consider a blunder (m)
    float blunder_minimum = 10.0;

  /// Percentage of predicted depth to be considered
  /// a blunder, if more than the minimum (0<p<1, typ. 0.25).
    float blunder_percent = 0.25;

  /// Scale on initialisation surface std. dev. at a node
  /// to allow before considering deep spikes to be blunders.
    float blunder_scalar = 3.0;

  /// Scale on predicted or estimated depth for how far out
  /// to accept data.  (unitless; typically 0.05 for
  /// hydrography but can be greater for geological mapping
  /// in flat areas with sparse data)
    float capture_distance_scale = 0.05;

  /// Per-beam backscatter angular-response correction mode (ADR-0007 D3).
  /// Default None = identity (corrected == raw). Set via
  /// GeoMapSheet::setBackscatterCorrection().
    BackscatterAngleCorrection backscatter_angle_correction =
      BackscatterAngleCorrection::None;

  /// Empirical angular-response curve: ascending {abs_angle_deg, db_relative_to_nadir}
  /// pairs (nadir bin ~0, off-nadir bins <= 0), loaded at startup from a per-sonar
  /// CSV. Empty -> the Empirical correction is a no-op (logged as a warning at
  /// configure time). Used only when backscatter_angle_correction == Empirical.
    std::vector < std::pair < float, float >> angular_response_curve;

  /// Tier-2 backscatter correction (cube_bathymetry#87): when true, the loaded
  /// angular_response_curve is a TL-REMOVED residual, so the estimator removes
  /// the per-beam 2-way transmission loss `40*log10(R) + 2*alpha*R` (R = per-beam
  /// slant range, m) BEFORE subtracting the residual curve, making the curve
  /// depth/range transferable. Set by the curve loader from the CSV header
  /// (`# tl_removed: <bool>`). Default false = tier-1 (no TL term), fully
  /// backward compatible. Used only when backscatter_angle_correction ==
  /// Empirical.
    bool backscatter_tl_removed = false;

  /// Tier-2 absorption coefficient alpha in dB/m (cube_bathymetry#87). The scalar
  /// the estimator multiplies into the TL term `2*alpha*R`; read verbatim from
  /// the curve CSV header (`# absorption_db_per_m: <float>`) so the C++ estimator
  /// NEVER recomputes the (Francois-Garrison) absorption -- the Python derive tool
  /// is the single source of truth, guaranteeing Python/C++ consistency. Default
  /// 0 = no absorption term (tier-1, or fresh water at negligible alpha).
    float backscatter_absorption_db_per_m = 0.0f;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__PARAMETERS_H_
