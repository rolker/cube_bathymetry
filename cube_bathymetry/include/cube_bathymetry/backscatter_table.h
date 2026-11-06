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


#ifndef CUBE_BATHYMETRY__BACKSCATTER_TABLE_H_
#define CUBE_BATHYMETRY__BACKSCATTER_TABLE_H_

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file
/// @brief Per-pulse-mode backscatter table (`angular-response-table v1`,
///        cube_bathymetry#172) and the mode-key spec the estimator shares
///        with `derive_angular_response.py`.
///
/// A sonar that switches pulse modes (the Kongsberg M3 has five) reads 10 to
/// 17 dB apart between modes at equal range, and applies its own range gain
/// per mode. The derive tool measures, per mode, an angular curve `A(|theta|)`
/// at a reference range R0, an empirical range term `r_g(log10(R / R0))` per
/// angle group `g`, and the mode's level offset against one reference mode.
/// This header holds the loaded table and the per-beam lookup the estimator
/// applies at record time:
///
///   corrected = raw - db_relative_to_nadir(|theta|) - r_g(log10(R / R0)) - offset
///
/// which puts every mode on the reference mode's nadir level at R0. The
/// table file is the contract (README, "Per-mode tables"); this loader reads
/// it strictly -- a row it cannot parse, or a mode whose per-mode scalars
/// disagree between rows, fails the load rather than being skipped, because
/// a half-read table corrects every beam it touches by the wrong amount.
///
/// The mode KEY is the shared spec: `<TYPE>_<pulse_us>us_<bw_Hz>Hz` per
/// transmit sector (joined with `+`), then `_<freq_kHz>kHz`, e.g.
/// `FMU_1000us_24000Hz_500kHz`. `canonicalModeKey` must produce the same
/// string as the Python `canonical_mode_key` for the same inputs;
/// `test/data/mode_keys.json` holds the shared vectors and both sides are
/// tested against it.

namespace cube
{

/// Reserved values of `Sounding::correction_idx` (cube_bathymetry#172).
///
/// 0 = unresolved: the ping's mode was never resolved (no table, no
/// SonarInfo yet, or the fallback policy chose identity for an unknown
/// mode). The estimator then applies the LEGACY single-curve correction if
/// one is configured (`Parameters::angular_response_curve`, the `*` entry),
/// else identity -- which is exactly the pre-#172 behaviour, so every
/// existing caller and test keeps its values.
  constexpr uint16_t kCorrectionUnresolved = 0;
/// The beam's backscatter is EXCLUDED: `correctBeamIntensity` returns NaN and
/// `Hypothesis::recordBeam` skips it (its depth is still used). The fallback
/// policy `exclude` resolves an uncorrectable ping to this value.
  constexpr uint16_t kCorrectionExclude = 0xFFFE;
/// Explicit identity: the beam is recorded uncorrected whatever curves or
/// tables are configured.
  constexpr uint16_t kCorrectionIdentity = 0xFFFF;
/// Table entries occupy 1 .. kMaxCorrectionEntries (entry k is index k - 1).
  constexpr uint16_t kMaxCorrectionEntries = 0xFFFD;

/// One number of a mode key: 3 significant figures, plain decimal
/// (`1000`, `24000`, `0.45`); `?` for a NaN, infinite or non-positive raw
/// value (tested BEFORE scaling) or a non-finite / non-positive scaled one.
/// Mirrors the Python `key_number` exactly (C `%.2e`, then rewritten without
/// exponent, trailing fractional zeros or trailing point).
  std::string keyNumber(double value, double scale = 1.0);

/// The canonical pulse-mode key of a ping (the shared spec above).
///
/// @param pulse_lengths_s, bandwidths_hz, tx_signal_types  the latest
///        SonarInfo at or before the ping, one element per transmit sector
///        in sector order (float32 message values).
/// @param frequency_hz  the ping's own `PingInfo.frequency`.
///
/// TYPE: `tx_signal_types` 1 -> CW, 2 -> FMU, 3 -> FMD; anything else or a
/// missing element -> UNK (an empty array means UNK for every sector, never
/// CW). The sector count is the longest of the three arrays; a SonarInfo
/// with all three empty gives one `UNK_?us_?Hz` sector. Missing elements are
/// `?`. The key always starts with a letter and never contains a comma or
/// whitespace; `*` is reserved.
  std::string canonicalModeKey(
    const std::vector < float > &pulse_lengths_s,
    const std::vector < float > &bandwidths_hz,
    const std::vector < uint8_t > &tx_signal_types,
    float frequency_hz);

/// The TVG state a stream declared (`SonarInfo.tvg_model`), as the table
/// records it per mode. `Mixed` is written by the derive tool for a mode seen
/// under more than one declared state; it matches no live declaration.
  enum class DeclaredTvg
  {
    Unknown,
    None,
    Log20R,
    Log40R,
    Custom,
    Mixed,
  };

  const char * toString(DeclaredTvg tvg);
/// Parse a table column value (`UNKNOWN`, `NONE`, `20LOGR`, `40LOGR`,
/// `CUSTOM`, `MIXED`). Returns false on anything else.
  bool parseDeclaredTvg(const std::string & text, DeclaredTvg & out);
/// Map a `SonarInfo.tvg_model` enum value (0..4) to the declared state;
/// out-of-range values are Unknown (the message's own default).
  DeclaredTvg declaredTvgFromMessageValue(uint8_t tvg_model);

/// Declared gain state of a stream: the model plus the absorption its TVG
/// used (NaN = undeclared). Two states are equal when the models match and
/// both absorptions are NaN or both agree within a relative 1e-5 (the table
/// writes the absorption with `%.6g`, so an exact comparison would flag a
/// declared value against its own round trip).
  struct DeclaredTvgState
  {
    DeclaredTvg model = DeclaredTvg::Unknown;
    float absorption_db_per_km = std::numeric_limits < float > ::quiet_NaN();

    bool operator == (const DeclaredTvgState & other) const;
    bool operator != (const DeclaredTvgState & other) const {return !(*this == other);}
  /// `<model>` or `<model>/<absorption> dB/km`, for reports.
    std::string toString() const;
  };

/// One `|theta|` bin of a mode's angular curve (a `term=angle` row).
  struct AngleBin
  {
  /// Bin centre, degrees from nadir.
    double abs_angle_deg = 0.0;
  /// `A`: the mode's level at R0 in this bin (dB).
    double value_db = 0.0;
  /// `A` minus the nadir-most bin's `A` (dB); what the correction subtracts.
    double db_relative_to_nadir = 0.0;
  /// Sample variance of the beams in the bin (dB^2); NaN when the table
  /// wrote it EMPTY (n < 2, or under 0.0005 dB^2) -- a beam in such a bin
  /// cannot be weighted and is uncorrectable by this table.
    double var_db2 = std::numeric_limits < double > ::quiet_NaN();
  /// Standard error of `A` (dB); NaN when empty.
    double se_db = std::numeric_limits < double > ::quiet_NaN();
    uint64_t n = 0;

    bool weighable() const {return var_db2 == var_db2 && var_db2 > 0.0;}
  };

/// One node of a range term (a `term=range` row).
  struct RangeNode
  {
  /// `log10(R / R0)`.
    double x = 0.0;
  /// `r_g` at the node (dB).
    double value_db = 0.0;
    double se_db = std::numeric_limits < double > ::quiet_NaN();
    uint64_t n = 0;
  };

/// The range term of one angle group: nodes ascending in `x`, linear between
/// them, clamped beyond the end nodes.
  struct RangeTermGroup
  {
    double angle_lo_deg = 0.0;
    double angle_hi_deg = 90.0;
    std::vector < RangeNode > nodes;

  /// `r_g(x)`: linear between nodes, clamped beyond the ends; 0 when the
  /// group has no nodes.
    double at(double x) const;
  };

/// One mode's entry: angular curve, range term per angle group, offset,
/// declared state and the quality record from the header.
  struct ModeEntry
  {
    std::string key;
  /// Ascending in `abs_angle_deg`.
    std::vector < AngleBin > angle;
  /// Ascending in `angle_lo_deg`. With the header's
  /// `# range_angle_groups_deg:` line, EVERY declared group, a group the
  /// writer did not fit (no range rows) being present with no nodes (a zero
  /// term); without that line, the groups the rows name, possibly none.
    std::vector < RangeTermGroup > groups;
  /// False for a mode with no switch path to the reference mode: it has no
  /// measured offset and MUST NOT be corrected by this table.
    bool offset_anchored = false;
    double offset_db_vs_ref = std::numeric_limits < double > ::quiet_NaN();
    double offset_se_db = std::numeric_limits < double > ::quiet_NaN();
    DeclaredTvgState declared;
  /// From the header's `# mode_summary:` line, which must carry `pings=` and
  /// appear at most once per mode; 0 when the mode has no summary line (a
  /// hand-made table), so any positive ping floor treats it as absent.
    uint64_t pings = 0;
    uint64_t beams = 0;
  /// Bins whose variance is EMPTY (uncorrectable beams fall in them).
    std::size_t unweighable_bins = 0;

  /// `db_relative_to_nadir` at `abs_angle_deg`: linear between bin centres,
  /// clamped to the first / last bin beyond them (the legacy curve's rule).
    double relativeDb(double abs_angle_deg) const;
  /// The bin whose centre is nearest to `abs_angle_deg` (null when the
  /// curve is empty).
    const AngleBin * binFor(double abs_angle_deg) const;
  /// The range-term group for `abs_angle_deg` per the table's lookup rule:
  /// `angle_lo_deg <= |theta| < angle_hi_deg`; below the first
  /// `angle_lo_deg` (the specular band) the first group -- the first
  /// DECLARED group when the header declares them, so an unfitted first
  /// group gives the specular band its zero term rather than the next
  /// group's; null when no group applies (zero term).
    const RangeTermGroup * groupFor(double abs_angle_deg) const;
  };

/// A loaded `angular-response-table v1`.
  struct BackscatterTable
  {
  /// Where it was read from (reports and the fingerprint).
    std::string path;
    std::string reference_mode;
  /// R0 in metres (`# range_ref_m:`).
    double range_ref_m = std::numeric_limits < double > ::quiet_NaN();
    double range_node_spacing_decades = std::numeric_limits < double > ::quiet_NaN();
    double angle_bin_width_deg = std::numeric_limits < double > ::quiet_NaN();
    double specular_exclusion_deg = std::numeric_limits < double > ::quiet_NaN();
  /// `# source_bags:` verbatim (reports).
    std::string source_bags;
  /// Entry k corresponds to `correction_idx == k + 1`.
    std::vector < ModeEntry > entries;

    bool empty() const {return entries.empty();}
  /// The `correction_idx` of a mode key (1-based), or nullopt when the table
  /// has no entry for it.
    std::optional < uint16_t > find(const std::string & key) const;
  /// The entry a `correction_idx` names, or null for a reserved / out of
  /// range value.
    const ModeEntry * entry(uint16_t correction_idx) const;

private:
    friend BackscatterTable loadBackscatterTable(
      const std::string &, std::string &, std::string *);
    std::unordered_map < std::string, uint16_t > index_;
  };

/// True when the file's first non-blank line (a UTF-8 byte-order mark
/// ignored) starts with `# format: angular-response-table` -- tables are
/// detected by that line, never guessed from rows. Any version counts, so a
/// table this loader cannot read (`v2`, a garbled version) still goes to
/// `loadBackscatterTable` and fails there loudly instead of being read as an
/// empty single curve. False for a missing or unreadable file, and for a
/// path that is not a regular file (a FIFO, a device): the probe would block
/// on it or consume what the loader needs, and reads at most 64 KiB.
  bool isAngularResponseTable(const std::string & path);

/// Load a table. On any failure returns an EMPTY table and sets @p error
/// (the file, the line, and why); a table is read whole or not at all.
/// The file is read ONCE, into memory, bounded (at most 64 MiB; a path that
/// is not a regular file is refused before it is opened), and parsed from
/// that buffer; @p bytes, when given, receives that same buffer, so a
/// fingerprint hashes exactly the table that was applied even if the file is
/// rewritten in place after the read (cube_bathymetry#172 review round 2).
  BackscatterTable loadBackscatterTable(
    const std::string & path, std::string & error, std::string * bytes = nullptr);

/// What the estimator does with a beam the table cannot correct: a ping of
/// a mode with no entry, an unanchored mode, a mode under the ping floor, a
/// ping before the first SonarInfo, a beam in an unweighable angle bin, a
/// beam at or beyond 90 deg, or a beam with no finite angle or no finite
/// positive range.
  enum class BackscatterModeFallback
  {
  /// Record the beam uncorrected (`correction_idx` 0 -> legacy path, which
  /// with a table and no legacy curve is identity).
    Identity,
  /// Drop the beam's backscatter; its depth is still used.
    Exclude,
  };

  const char * toString(BackscatterModeFallback fallback);
/// Parse `identity` / `exclude` (case-insensitive). False on anything else.
  bool parseBackscatterModeFallback(const std::string & text, BackscatterModeFallback & out);

/// Whether the table corrects one beam, and if not why (cube_bathymetry#172).
  enum class TableBeamStatus
  {
    Corrected,
  /// @p correction_idx names no entry (a reserved or out-of-range value).
    NoEntry,
  /// The entry has no measured offset.
    Unanchored,
  /// The beam angle is NaN / infinite.
    NoFiniteAngle,
  /// `|theta| >= 90` deg: beyond every bin and group the writer can produce
  /// (it drops such beams), so neither the angle nor the range term is
  /// measured there.
    AngleAtOrBeyond90,
  /// The beam lies more than half `angle_bin_width_deg` outside the written
  /// angle bins (beyond the outermost centre, or short of the nadir-most
  /// one): no bin the writer measured covers it, and the clamped end value
  /// is not a measurement there (the same reasoning as the 90 deg rule). A
  /// gap BETWEEN written bins is interpolated between the two measured
  /// neighbours and is not this case; a table without
  /// `# angle_bin_width_deg:` skips the test.
    OutsideAngleBins,
  /// No finite positive slant range (the range term needs `log10 R`).
    NoFiniteRange,
  /// The nearest angle bin's variance is EMPTY (unweighable).
    UnweighableBin,
  };

  const char * toString(TableBeamStatus status);

/// The status `correctWithTable` acts on: Corrected, or the first reason the
/// beam is uncorrectable, in the order of the enum.
  TableBeamStatus tableBeamStatus(
    double abs_angle_deg, double range_m, const BackscatterTable & table,
    uint16_t correction_idx);

/// `|beam_angle|` in degrees (NaN for a NaN angle): the conversion the
/// estimator applies before a table lookup.
  double beamAbsAngleDeg(float beam_angle_rad);

/// Per-beam table correction. Returns the corrected level, @p raw_db when
/// the fallback is Identity and the beam is uncorrectable
/// (`tableBeamStatus` is not Corrected), or NaN when the fallback is
/// Exclude and it is. @p correction_idx must name a table entry (1-based;
/// the caller handles the reserved values).
  double correctWithTable(
    double raw_db, double abs_angle_deg, double range_m,
    const BackscatterTable & table, uint16_t correction_idx,
    BackscatterModeFallback fallback);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BACKSCATTER_TABLE_H_
