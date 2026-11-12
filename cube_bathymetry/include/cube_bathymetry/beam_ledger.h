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

#ifndef CUBE_BATHYMETRY__BEAM_LEDGER_H_
#define CUBE_BATHYMETRY__BEAM_LEDGER_H_

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "cube_bathymetry/derived_tiles.h"

/// @file
/// @brief The beam ledger of a slope run (cube_bathymetry#173, second part): the
///        incidence angle each beam was given, so that
///        `derive_angular_response.py --angle-basis incidence` bins by exactly the
///        angle the import will apply. One source of truth: the Python tool never
///        recomputes an angle.
///
/// ### Format (all integers little-endian)
/// ```
/// "CUBELDG1"                      8 bytes
/// header_length                   uint32
/// header                          UTF-8 JSON, header_length bytes
/// then, until the end of the file, one record per ping:
///   stamp_ns                      int64   the ping's header stamp
///   bag_index                     uint16  index into header "bags"
///   beam_count                    uint16
///   beam_count x {
///     beam_index                  uint16  index in the ping's detection arrays
///     incidence                   int16   0.01 deg; -32768 = none
///     source                      uint8   0 = surface normal; 1..6 = vertical
///                                          used (marine_backscatter FallbackReason);
///                                          7 = no beam vector; 8 = invalid angle
///   }
/// ```
/// The header holds `format` ("beam-ledger v1"), `code_version`, the store-level
/// `inputs` of the run (as in derived_tiles.json: baseline, layers, correction,
/// gain declaration, level, ...), and `bags` (name and metadata.yaml SHA-256, in
/// the order `bag_index` refers to). Only the beams that survived the projector's
/// range gate are listed; a beam absent from a ping's record was dropped.
///
/// The writer produces the file atomically (`<path>.partial`, fsync, rename) and
/// only when `finish` is called, which a run that did not complete never does.

namespace cube
{

  constexpr const char * kBeamLedgerMagic = "CUBELDG1";
  constexpr const char * kBeamLedgerFormat = "beam-ledger v1";
  constexpr int16_t kLedgerNoIncidence = -32768;

  struct LedgerBeam
  {
    uint16_t beam_index = 0;
    int16_t incidence_centideg = kLedgerNoIncidence;
    uint8_t source = 255;
    bool operator == (const LedgerBeam & o) const
    {
      return beam_index == o.beam_index && incidence_centideg == o.incidence_centideg &&
             source == o.source;
    }
  };

  struct LedgerPing
  {
    int64_t stamp_ns = 0;
    uint16_t bag_index = 0;
    std::vector < LedgerBeam > beams;
  };

/// Radians to the ledger's 0.01 deg integer; NaN (and anything outside int16) is
/// kLedgerNoIncidence.
  int16_t ledgerIncidence(float radians);

  class BeamLedgerWriter
  {
public:
  /// Open `<path>.partial` and write the header. Null with @p error set if the
  /// file cannot be created (a missing directory, say), so a slope run fails
  /// before it reads a bag.
    static std::unique_ptr < BeamLedgerWriter > open(
      const std::string & path, const StoreInputs & inputs, const std::vector < BagIdentity > &bags,
      std::string & error);

  /// Removes the partial file unless @ref finish succeeded.
    ~BeamLedgerWriter();
    BeamLedgerWriter(const BeamLedgerWriter &) = delete;
    BeamLedgerWriter & operator = (const BeamLedgerWriter &) = delete;

    void addPing(int64_t stamp_ns, uint16_t bag_index, const std::vector < LedgerBeam > &beams);
    uint64_t pings() const {return pings_;}

  /// Flush, fsync and rename into place. @throws std::runtime_error on failure.
    void finish();

  /// Delete the file `finish` made (the run failed after it).
    void discard();

private:
    explicit BeamLedgerWriter(const std::string & path)
    : path_(path) {
    }
    std::string path_;
    std::ofstream out_;
    uint64_t pings_ = 0;
    bool finished_ = false;
  };

/// A ledger read back whole (tests and tools).
  struct BeamLedger
  {
    std::string header_json;
    std::vector < LedgerPing > pings;
  };

/// @throws std::runtime_error naming what is wrong (magic, truncation, JSON).
  BeamLedger readBeamLedger(const std::string & path);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BEAM_LEDGER_H_
