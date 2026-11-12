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

#include "cube_bathymetry/beam_ledger.h"

#include <unistd.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "cube_bathymetry/build_fingerprint.h"

static_assert(
  __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
  "the beam ledger is written and read little-endian by copying native integers");

namespace cube
{

namespace fs = std::filesystem;

namespace
{

template<typename T>
void putLe(std::ostream & out, T value)
{
  unsigned char bytes[sizeof(T)];
  std::memcpy(bytes, &value, sizeof(T));   // the host is little-endian (x86-64, aarch64)
  out.write(reinterpret_cast<const char *>(bytes), sizeof(T));
}

template<typename T>
T getLe(const std::string & data, std::size_t & pos)
{
  if (pos + sizeof(T) > data.size()) {
    throw std::runtime_error("beam ledger: truncated record");
  }
  T value;
  std::memcpy(&value, data.data() + pos, sizeof(T));
  pos += sizeof(T);
  return value;
}

}  // namespace

int16_t ledgerIncidence(float radians)
{
  if (!std::isfinite(radians)) {
    return kLedgerNoIncidence;
  }
  const double centideg = std::round(static_cast<double>(radians) * 180.0 / M_PI * 100.0);
  if (centideg < -32767.0 || centideg > 32767.0) {
    return kLedgerNoIncidence;
  }
  return static_cast<int16_t>(centideg);
}

std::unique_ptr<BeamLedgerWriter> BeamLedgerWriter::open(
  const std::string & path, const StoreInputs & inputs, const std::vector<BagIdentity> & bags,
  const std::string & detections_topic, std::string & error)
{
  error.clear();
  std::unique_ptr<BeamLedgerWriter> w(new BeamLedgerWriter(path));
  // Unique per process, so two runs writing ledgers cannot share a temporary file.
  const std::string partial = path + ".partial-" + std::to_string(::getpid());
  w->partial_ = partial;
  w->out_.open(partial, std::ios::binary | std::ios::trunc);
  if (!w->out_) {
    error = "cannot create the beam ledger " + partial;
    return nullptr;
  }
  nlohmann::json header;
  header["format"] = kBeamLedgerFormat;
  header["code_version"] = kDerivedTilesCodeVersion;
  header["detections_topic"] = detections_topic;
  header["inputs"] = nlohmann::json::parse(storeInputsJson(inputs));
  header["incidence_unit_deg"] = 0.01;
  header["incidence_none"] = kLedgerNoIncidence;
  nlohmann::json bag_list = nlohmann::json::array();
  for (const BagIdentity & b : bags) {
    bag_list.push_back({{"name", b.name}, {"metadata_sha256", b.metadata_sha256}});
  }
  header["bags"] = bag_list;
  const std::string text = header.dump();
  w->out_.write(kBeamLedgerMagic, 8);
  putLe<uint32_t>(w->out_, static_cast<uint32_t>(text.size()));
  w->out_.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!w->out_) {
    error = "cannot write the beam ledger " + partial;
    return nullptr;
  }
  return w;
}

BeamLedgerWriter::~BeamLedgerWriter()
{
  if (!finished_) {
    out_.close();
    std::error_code ec;
    fs::remove(partial_, ec);
  }
}

void BeamLedgerWriter::addPing(
  int64_t stamp_ns, uint16_t bag_index, const std::vector<LedgerBeam> & beams)
{
  if (beams.size() > 0xFFFF) {
    throw std::runtime_error(
            "beam ledger: a ping of " + std::to_string(beams.size()) +
            " beams does not fit the format (65535 at most)");
  }
  const std::size_t count = beams.size();
  putLe<int64_t>(out_, stamp_ns);
  putLe<uint16_t>(out_, bag_index);
  putLe<uint16_t>(out_, static_cast<uint16_t>(count));
  for (std::size_t i = 0; i < count; ++i) {
    putLe<uint16_t>(out_, beams[i].beam_index);
    putLe<int16_t>(out_, beams[i].incidence_centideg);
    putLe<uint8_t>(out_, beams[i].source);
  }
  ++pings_;
}

std::string BeamLedgerWriter::seal()
{
  out_.flush();
  if (!out_) {
    throw std::runtime_error("beam ledger: write error on " + partial_);
  }
  out_.close();
  fsyncFile(partial_);
  sealed_ = true;
  return sha256File(partial_);
}

void BeamLedgerWriter::commit()
{
  if (!sealed_) {
    throw std::runtime_error("beam ledger: commit before seal");
  }
  std::error_code ec;
  fs::rename(partial_, path_, ec);
  if (ec) {
    throw std::runtime_error("beam ledger: cannot rename " + partial_ + ": " + ec.message());
  }
  finished_ = true;
  try {
    fsyncDirectory(
      fs::path(path_).parent_path().empty() ? "." : fs::path(path_).parent_path().string());
  } catch (const std::exception &) {
    // Durability of the rename only; the ledger is in place.
  }
}

void BeamLedgerWriter::finish()
{
  seal();
  commit();
}

void BeamLedgerWriter::discard()
{
  std::error_code ec;
  fs::remove(path_, ec);
}

BeamLedger readBeamLedger(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("beam ledger: cannot read " + path);
  }
  const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (data.size() < 12 || data.compare(0, 8, kBeamLedgerMagic) != 0) {
    throw std::runtime_error("beam ledger: " + path + " is not a beam ledger (bad magic)");
  }
  std::size_t pos = 8;
  const uint32_t header_len = getLe<uint32_t>(data, pos);
  if (pos + header_len > data.size()) {
    throw std::runtime_error("beam ledger: truncated header");
  }
  BeamLedger ledger;
  ledger.header_json = data.substr(pos, header_len);
  pos += header_len;
  while (pos < data.size()) {
    LedgerPing ping;
    ping.stamp_ns = getLe<int64_t>(data, pos);
    ping.bag_index = getLe<uint16_t>(data, pos);
    const uint16_t count = getLe<uint16_t>(data, pos);
    ping.beams.resize(count);
    for (LedgerBeam & b : ping.beams) {
      b.beam_index = getLe<uint16_t>(data, pos);
      b.incidence_centideg = getLe<int16_t>(data, pos);
      b.source = getLe<uint8_t>(data, pos);
    }
    ledger.pings.push_back(std::move(ping));
  }
  return ledger;
}

}  // namespace cube
