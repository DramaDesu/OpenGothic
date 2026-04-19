// dmaudit — standalone DirectMusic .sty/.sgt curve-usage auditor.
//
// Walks a directory tree, parses every RIFF-based DirectMusic file it finds,
// and reports a histogram of pattern-curve types (bEventType × bCCData).
//
// The goal is to answer, empirically, which MIDI CCs and event-curves Gothic
// actually authored — so the synth engine can skip implementing features that
// are never used in the real data.
//
// Depends only on game/dmusic/riff.{h,cpp}. No Tempest, no engine links.
//
// Build: see CMakeLists.txt in this folder.
// Usage: dmaudit <path-to-music-root>

#include "riff.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── DMUS_IO_STYLECURVE (mirrors game/dmusic/structs.h) ────────────────────────
#pragma pack(push,1)
struct DMUS_IO_STYLECURVE {
  uint32_t mtGridStart;
  uint32_t dwVariation;
  uint32_t mtDuration;
  uint32_t mtResetDuration;
  int16_t  nTimeOffset;
  uint16_t nStartValue;
  uint16_t nEndValue;
  uint16_t nResetValue;
  uint8_t  bEventType;   // 0=NULL, 3=PB, 4=CC, 5=MAT, 6=PAT, 7=RPN, 8=NRPN
  uint8_t  bCurveShape;
  uint8_t  bCCData;
  uint8_t  bFlags;
  uint16_t wParamType;
  uint16_t wMergeIndex;
  };
#pragma pack(pop)

static const char* curveTypeName(uint8_t t) {
  switch(t) {
    case 0: return "NULL";
    case 3: return "PB";
    case 4: return "CC";
    case 5: return "MAT";
    case 6: return "PAT";
    case 7: return "RPN";
    case 8: return "NRPN";
    default: return "?";
    }
  }

static const char* ccName(uint8_t cc) {
  switch(cc) {
    case 0x00: return "BankSelect";
    case 0x01: return "ModWheel";
    case 0x07: return "ChannelVolume";
    case 0x0A: return "Pan";
    case 0x0B: return "Expression";
    case 0x40: return "SustainPedal";
    case 0x5B: return "ReverbSend";
    case 0x5D: return "ChorusSend";
    case 0x79: return "ResetAll";
    case 0x7B: return "AllNotesOff";
    default:   return "";
    }
  }

struct Stats {
  // (eventType<<8 | ccData) → count
  std::map<uint16_t, uint64_t> histogram;
  uint64_t filesParsed   = 0;
  uint64_t filesSkipped  = 0;
  uint64_t patternsFound = 0;
  uint64_t curvesFound   = 0;
  // Range of curve values seen per (type,cc) to spot obvious bugs.
  struct Range { uint16_t minStart = 0xFFFF, maxStart = 0; uint16_t minEnd = 0xFFFF, maxEnd = 0; };
  std::map<uint16_t, Range>   ranges;
  // elemSize (bytes per DMUS_IO_STYLECURVE record on disk) → curve count.
  std::map<uint32_t, uint64_t> elemSizeHistogram;
  // Up to 12 raw-ish dumps of curves whose values fall outside the expected range.
  std::vector<std::string>    oobDumps;
  };

// Walk a RIFF/LIST body (4-byte list-id already consumed) looking for crve.
static void walkBody(Dx8::Riff& body, Stats& stats) {
  body.read([&](Dx8::Riff& c){
    // RIFF / LIST chunks start with a 4-byte list-id that Riff does NOT
    // auto-consume; we must call readListId() before descending.
    if(c.is("RIFF") || c.is("LIST")) {
      try { c.readListId(); } catch(...) { return; }
      walkBody(c, stats);
      return;
      }
    if(c.is("crve")) {
      // Record the on-disk element size so we can tell whether Gothic used
      // DX7 (28-byte) or DX8 (32-byte) DMUS_IO_STYLECURVE layout.
      const size_t before = c.remaning();
      std::vector<DMUS_IO_STYLECURVE> curves;
      uint32_t elemSize = 0;
      try {
        std::memcpy(&elemSize, reinterpret_cast<const uint8_t*>(&before) - 0, 0); // no-op, keep type
        c.readAll(curves);
      } catch(...) { return; }
      // Re-derive elemSize: (bytes before readAll - 4 header) / curves.size().
      if(!curves.empty() && before >= 4)
        elemSize = uint32_t((before - 4) / curves.size());
      stats.elemSizeHistogram[elemSize] += uint64_t(curves.size());
      stats.curvesFound += curves.size();
      for(auto& cu : curves) {
        const uint16_t key = uint16_t((uint16_t(cu.bEventType) << 8) | uint16_t(cu.bCCData));
        stats.histogram[key]++;
        auto& r = stats.ranges[key];
        if(cu.nStartValue < r.minStart) r.minStart = cu.nStartValue;
        if(cu.nStartValue > r.maxStart) r.maxStart = cu.nStartValue;
        if(cu.nEndValue   < r.minEnd)   r.minEnd   = cu.nEndValue;
        if(cu.nEndValue   > r.maxEnd)   r.maxEnd   = cu.nEndValue;

        // Flag out-of-band values (>127 for CC/MAT, >16383 for PB/RPN/NRPN).
        const bool isCC  = (cu.bEventType == 4);
        const bool isMAT = (cu.bEventType == 5);
        const bool isPBish = (cu.bEventType == 3 || cu.bEventType == 7 || cu.bEventType == 8);
        const bool oob =
          ((isCC || isMAT) && (cu.nStartValue > 127 || cu.nEndValue > 127)) ||
          (isPBish && (cu.nStartValue > 16383 || cu.nEndValue > 16383));
        if(oob && stats.oobDumps.size() < 12) {
          char buf[256];
          std::snprintf(buf, sizeof(buf),
            "    [%s cc=%u] start=%u end=%u  dur=%u shape=%u flags=%u  (elemSize=%u)",
            curveTypeName(cu.bEventType), unsigned(cu.bCCData),
            unsigned(cu.nStartValue), unsigned(cu.nEndValue),
            unsigned(cu.mtDuration), unsigned(cu.bCurveShape),
            unsigned(cu.bFlags), unsigned(elemSize));
          stats.oobDumps.push_back(buf);
          }
        }
      return;
      }
    if(c.is("ptnh")) {
      stats.patternsFound++;
      }
    // Not a known parent — skip silently.
    });
  }

static bool auditFile(const fs::path& p, Stats& stats) {
  std::ifstream f(p, std::ios::binary);
  if(!f) { stats.filesSkipped++; return false; }
  f.seekg(0, std::ios::end);
  const std::streamoff sz = f.tellg();
  if(sz <= 12) { stats.filesSkipped++; return false; }
  f.seekg(0, std::ios::beg);
  const size_t        byteSize = size_t(sz);
  std::vector<uint8_t> data;
  data.resize(byteSize);
  f.read(reinterpret_cast<char*>(data.data()), std::streamsize(byteSize));

  try {
    Dx8::Riff root(data.data(), data.size());
    if(!root.is("RIFF")) { stats.filesSkipped++; return false; }
    // Consume the 4-byte RIFF list-id (e.g. "DMST"/"DMSG") before recursing.
    root.readListId();
    walkBody(root, stats);
    stats.filesParsed++;
    return true;
  } catch(const std::exception& e) {
    std::fprintf(stderr, "  (parse error in %s: %s)\n", p.string().c_str(), e.what());
    stats.filesSkipped++;
    return false;
    }
  }

static bool isMusicFile(const fs::path& p) {
  auto ext = p.extension().string();
  for(auto& c : ext) c = char(std::tolower(unsigned(c)));
  return ext == ".sty" || ext == ".sgt";
  }

int main(int argc, char** argv) {
  if(argc < 2) {
    std::fprintf(stderr,
      "Usage: %s <music-root>\n"
      "\n"
      "Walks the directory tree, parses every .sty/.sgt DirectMusic file,\n"
      "and prints a histogram of pattern-curve usage.\n",
      argv[0]);
    return 1;
    }

  fs::path root = argv[1];
  if(!fs::exists(root)) {
    std::fprintf(stderr, "path does not exist: %s\n", root.string().c_str());
    return 2;
    }

  Stats stats;
  for(auto& entry : fs::recursive_directory_iterator(root)) {
    if(!entry.is_regular_file())    continue;
    if(!isMusicFile(entry.path()))  continue;
    auditFile(entry.path(), stats);
    }

  std::printf("\n=== dmaudit summary ===\n");
  std::printf("root:             %s\n", root.string().c_str());
  std::printf("files parsed:     %llu\n", (unsigned long long)stats.filesParsed);
  std::printf("files skipped:    %llu\n", (unsigned long long)stats.filesSkipped);
  std::printf("patterns found:   %llu\n", (unsigned long long)stats.patternsFound);
  std::printf("curves total:     %llu\n", (unsigned long long)stats.curvesFound);

  if(stats.histogram.empty()) {
    std::printf("\n(no curves found)\n");
    return 0;
    }

  std::printf("\n%-5s %-4s %-14s %9s  value range (start → end)\n",
              "type", "cc", "name", "count");
  std::printf("-------------------------------------------------------------------\n");
  for(auto& [k, v] : stats.histogram) {
    const uint8_t et = uint8_t(k >> 8);
    const uint8_t cc = uint8_t(k & 0xFF);
    const char*   tn = curveTypeName(et);
    const char*   nn = (et == 4) ? ccName(cc) : "-";
    const auto&   r  = stats.ranges[k];
    std::printf("%-5s %3u  %-14s %9llu  [%u..%u] → [%u..%u]\n",
                tn, unsigned(cc), nn, (unsigned long long)v,
                unsigned(r.minStart), unsigned(r.maxStart),
                unsigned(r.minEnd),   unsigned(r.maxEnd));
    }

  // Practical verdict hints
  auto pick = [&](uint8_t et, uint8_t cc) -> uint64_t {
    auto it = stats.histogram.find(uint16_t((uint16_t(et)<<8) | uint16_t(cc)));
    return it == stats.histogram.end() ? 0 : it->second;
    };

  // Element size histogram (DX7 vs DX8 curve layout).
  std::printf("\n=== on-disk DMUS_IO_STYLECURVE element size ===\n");
  for(auto& [sz,n] : stats.elemSizeHistogram) {
    const char* note = "?";
    if(sz == 28) note = "DX7 layout (no wParamType/wMergeIndex)";
    else if(sz == 32) note = "DX8 layout";
    std::printf("  elemSize=%u bytes  curves=%llu  [%s]\n",
                unsigned(sz), (unsigned long long)n, note);
    }

  // Dumps of out-of-band curves.
  if(!stats.oobDumps.empty()) {
    std::printf("\n=== curves with out-of-band values (first %zu) ===\n", stats.oobDumps.size());
    for(auto& s : stats.oobDumps)
      std::printf("%s\n", s.c_str());
    }

  std::printf("\n=== feature verdict ===\n");
  auto verdict = [&](const char* what, uint64_t count){
    std::printf("  %-28s %9llu  %s\n", what, (unsigned long long)count,
                count == 0 ? "NOT USED — skip"
                : count < 10 ? "rare — low priority"
                : "USED — implement");
    };

  uint64_t anyPB = 0;
  for(auto& [k,v] : stats.histogram) if((k>>8) == 3) anyPB += v;
  uint64_t anyMAT = 0, anyPAT = 0, anyRPN = 0, anyNRPN = 0;
  for(auto& [k,v] : stats.histogram) {
    const uint8_t et = uint8_t(k>>8);
    if(et == 5) anyMAT += v;
    if(et == 6) anyPAT += v;
    if(et == 7) anyRPN += v;
    if(et == 8) anyNRPN += v;
    }

  verdict("CC7  Channel Volume",       pick(4, 0x07));
  verdict("CC11 Expression",           pick(4, 0x0B));
  verdict("CC10 Pan (time-varying)",   pick(4, 0x0A));
  verdict("CC1  Modulation",           pick(4, 0x01));
  verdict("CC64 Sustain Pedal",        pick(4, 0x40));
  verdict("CC91 Reverb Send",          pick(4, 0x5B));
  verdict("Pitch Bend curves",         anyPB);
  verdict("Channel Aftertouch (MAT)",  anyMAT);
  verdict("Poly Aftertouch (PAT)",     anyPAT);
  verdict("RPN",                       anyRPN);
  verdict("NRPN",                      anyNRPN);

  return 0;
  }
