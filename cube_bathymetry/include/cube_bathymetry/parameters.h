#ifndef CUBE_BATHYMETRY_PARAMETERS_H
#define CUBE_BATHYMETRY_PARAMETERS_H

#include "common.h"
#include <limits>
#include <cstdint>
#include <string>

namespace cube
{

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

  ///CUBE_PREDSURF uses the predicted surface depth and
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


  Parameters(CellSizes sizes, std::string iho_order = "order1a");
  void setIHOLimits(std::string order);
  void setGridResolution(CellSizes sizes);

  /// Value used to indicate 'no data' (typ. FLT_MAX)
  float no_data_value = std::numeric_limits<float>::quiet_NaN();

  /// Method used to extract information from sheet
  CubeExtractor extractor = CUBE_LHOOD;

  /// Depth to initialise estimates
  double nodata_depth = 0.0;

  /// Variance for initialisation
  double nodata_variance = 1.0e6;

  /// Exponent on distance for variance scale
  double distance_exponent = 2.0;

  /// 1.0/distance_exponent for efficiency
  double inverse_distance_exponent = 1.0/distance_exponent;

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
};

} // namespace cube

#endif