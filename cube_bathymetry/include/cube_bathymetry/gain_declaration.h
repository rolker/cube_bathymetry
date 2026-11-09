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


#ifndef CUBE_BATHYMETRY__GAIN_DECLARATION_H_
#define CUBE_BATHYMETRY__GAIN_DECLARATION_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cube_bathymetry/geo_sounding.h"

/// @file
/// @brief The operator-written gain-state declaration
///        (`gain-state-declaration v1`, cube_bathymetry#176).
///
/// A sonar's receiver gain can step without the sonar recording it (the
/// Appledore M3 stepped 23 dB low for two days; SonarInfo carries no gain).
/// A table derived across the step is fitted over two gain states, and every
/// import across it puts a seam in the mosaic. The operator declares the
/// steps: a REFERENCE state (a free label) plus the DEVIATIONS from it, as
/// half-open UTC intervals `[start, end)` carrying an additive dB offset --
/// the value ADDED to the reported level to reach the reference state:
///
///   level_in_reference = reported + offset_db
///
/// A segment whose sonar reported 23 dB low is therefore `+23.0`. A ping in no
/// segment is AT the reference (offset zero), not unknown; the resolver
/// counts such pings so a mistyped stamp shows. The file format and every
/// rule the loader enforces are in the README ("Gain-state declaration").
///
/// The offset applies to the raw intensity of the ping before the mode table
/// or any curve (`GainStateResolver`, `shiftSoundings`); it is a receiver
/// calibration, independent of the table. The declaration is campaign
/// configuration: this repository ships the schema, the loader and a test
/// fixture, never a campaign's file.

namespace cube
{

/// One declared deviation from the reference gain state.
  struct GainSegment
  {
    std::string id;
  /// Inclusive start, exclusive end: nanoseconds since the Unix epoch (UTC).
    int64_t start_ns = 0;
    int64_t end_ns = 0;
  /// Added to the reported level to reach the reference state.
    double offset_db = 0.0;
  /// `settings_file`, `measured` or `operator_reported`.
    std::string provenance;
  /// How the offset was obtained; mandatory and non-empty.
    std::string note;
  };

/// A loaded declaration. Segments are sorted by start and do not overlap.
  struct GainDeclaration
  {
  /// Where it was read from (reports; not part of the digest).
    std::string path;
  /// The label of the reference gain state; part of the digest.
    std::string reference_state;
  /// Free text from `# sonar:`; informational, not hashed.
    std::string sonar;
    std::vector < GainSegment > segments;
  /// Problems that do not refuse the file (e.g. zero segments).
    std::vector < std::string > warnings;

    bool loaded() const {return !reference_state.empty();}

  /// The segment containing @p stamp_ns (`[start, end)`), or null: the
  /// ping is then at the reference state.
    const GainSegment * segmentAt(int64_t stamp_ns) const;
  /// `segmentAt(stamp)->offset_db`, 0 when no segment contains it. The only
  /// question the consumers ask: a later source-side gain term (marine_tools
  /// #97) composes with it by sum.
    double offsetDbAt(int64_t stamp_ns) const;

  /// The canonical form the digest is taken of: the format version, the
  /// reference state and each segment's `start_ns`, `end_ns` and `offset_db`
  /// (`%.6f`, negative zero as zero) in sorted order, one per line. Ids,
  /// provenance, notes, `# sonar:`, comments, whitespace and field order are
  /// NOT in it, so editing a note does not make every store and table stale.
  /// `derive_angular_response.py` writes the same text; the shared vectors
  /// pin both. Hash it with `gainDeclarationSha256` (build_fingerprint.h).
    std::string canonicalForm() const;
  };

/// Load a declaration. On any failure returns an EMPTY declaration and sets
/// @p error (file, line and why): a declaration is read whole or not at all.
/// The file is read ONCE (at most 64 KiB; a path that is not a regular file is
/// refused), and @p bytes, when given, receives the buffer that was parsed.
  GainDeclaration loadGainDeclaration(
    const std::string & path, std::string & error, std::string * bytes = nullptr);

/// True when the first non-blank line of the regular file @p path starts with
/// `# format: gain-state-declaration` -- used to give a clear error when some
/// other kind of file is passed to a declaration option.
  bool isGainDeclaration(const std::string & path);

/// Parse `YYYY-MM-DDTHH:MM:SS[.f{1,9}]Z` (UTC only) to nanoseconds since the
/// Unix epoch. False for anything else: a numeric offset, a missing `Z`, an
/// impossible date, a leap second, a year outside 1970..2200.
  bool parseUtcNs(const std::string & text, int64_t & out);

/// What `GainStateResolver` counted.
  struct GainStateCounts
  {
    uint64_t pings = 0;
    uint64_t undeclared = 0;
    std::map < std::string, uint64_t > by_segment;
  };

/// Applies a declaration ping by ping and counts what it did.
  class GainStateResolver
  {
public:
  /// @p declaration must be loaded and must outlive the resolver.
    explicit GainStateResolver(const GainDeclaration * declaration);

  /// The offset to add to the intensity of a ping stamped @p ping_stamp_ns;
  /// counts the ping against its segment, or as undeclared (at reference).
    double apply(int64_t ping_stamp_ns);

    const GainStateCounts & counts() const {return counts_;}
    const GainDeclaration & declaration() const {return *declaration_;}

  /// A segment of the declaration that no ping fell in.
    std::vector < std::string > emptySegments() const;

  /// Human-readable report: reference state, source, one line per segment
  /// (`id [start, end) +offset dB (provenance): N pings`), the undeclared
  /// count, and a warning line per segment no ping fell in.
    std::string report() const;

private:
    const GainDeclaration * declaration_;
    GainStateCounts counts_;
  };

/// Whether a fitted curve or table (@p what, for the message) agrees with the
/// declaration an import is run under (cube_bathymetry#176, decision 8): a
/// curve fitted in the reference state and applied to un-shifted post-step
/// pings is wrong by the step, and one fitted across the step is invalid.
/// @p recorded_sha / @p recorded_reference are the file's `# gain_*` header
/// values (empty when it has none); @p declaration may be null (none given)
/// and @p declaration_sha is its canonical digest. Returns "" when they
/// agree (including: neither side has a declaration), else the reason.
  std::string gainRecordProblem(
    const std::string & what, const std::string & recorded_sha,
    const std::string & recorded_reference, const GainDeclaration * declaration,
    const std::string & declaration_sha);

/// Add @p offset_db to the intensity of every sounding whose intensity is
/// finite (a NaN stays NaN: it is no sample). Called for a ping of ANY
/// pulse mode, resolved or not, table or no table -- the offset is a
/// receiver calibration, not part of the mode correction.
  void shiftSoundings(std::vector < GeoSounding > &soundings, double offset_db);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__GAIN_DECLARATION_H_
