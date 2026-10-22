/**
 * @file    tag-txt-import.cc
 * @brief   Convert a legacy BitTag or PresTag .txt log to a SQLite log, and
 *          prove the conversion by rendering it back to text.
 *
 * @details The .txt format holds decoded values, not tag records, so no other
 *          path can read it. This tool parses one file, writes it with
 *          SqliteTagLogWriter's decoded-sample entry (writeSamples()) into the
 *          same tables a download writes, and then checks the result:
 *
 *              tag-txt-import log.txt -o log.db3
 *
 *          **The check.** The written database is read back, its rows are
 *          rebuilt into the records the text writer consumes, and those are
 *          rendered with TextTagLogWriter, the code that wrote current-format
 *          files. The rendered data lines are compared with the source file's
 *          line by line: byte for byte where they match, otherwise epochs and
 *          keys exactly and each number within half a unit of the source's
 *          last printed digit (older writers printed fewer digits). The
 *          header's fields are rendered the same way and compared field by
 *          field. A failed check writes no database (or, with --keep-failed,
 *          one marked failed) and exits non-zero.
 *
 *          **Variants** (host/libraries/tagcore/design/proposals/
 *          retire-text-logs.md): B, the BitTag V5 header; C, the current
 *          header, including C3/C4's fourth `V:` field; D, PresTag; E, no
 *          header.
 *
 *          **Variant A** (BitTagRTCv4 firmware, 2019–2020, written by the
 *          accel-tag repository's `monitor/Src/DumpPersistent.c`) is
 *          translated, not checked: no writer here can render it back. Each
 *          5-minute hex word holds five 6-bit counts of active seconds per
 *          minute, decoded as btviz decodes them: for i = 5 … 1, the count
 *          in bits (i−1)·6 is the activity at epoch − i·60, as count / 0.6
 *          percent. (Checked on the collection: no count exceeds 60 but a
 *          handful, and minutes correlate across record boundaries as they
 *          do within a word only in this order.)
 *          Bits 30–31 are not activity and are dropped, as btviz drops them;
 *          the report counts words with bit 30 set. `temperature T vdd V`
 *          lines become CoreTemperature and Voltage. The local-time text is
 *          ignored; the epoch is authoritative.
 *
 *          **Repairs, never silent.** Exact duplicate records are removed;
 *          conflicting duplicates are kept and counted; records outside
 *          2015 to the read time (plus a day) are dropped; backward time
 *          jumps are counted. A `V:` line with no activity before it, or
 *          activity no `V:` line closes (the ends of exports run together),
 *          fails the conversion rather than being guessed at. FIVEMIN files
 *          written by the pre-2021 host, with
 *          nine 7-bit buckets per record, are re-decoded as seven 9-bit ones
 *          (`fivemin-9x7`), and their check renders back through the buggy
 *          layout. Every repair is in the conversion report, which goes to the
 *          log's info table and, with --report, to a JSON file.
 *
 *          **PresTag voltages.** PresTag firmware put a second supply-voltage
 *          reading in the record's core-temperature slot, so its `TC:` field
 *          is a voltage. The two are stored as one Voltage row, their mean,
 *          and no CoreTemperature row, matching a direct SQLite download,
 *          which keeps one voltage. The round trip writes the mean back into
 *          both slots and compares it with the source's mean. The tool warns
 *          on every PresTag conversion.
 *
 *          **Provenance**, in the info table: source = txt-import, the
 *          source path (relative to --root when given), SHA-256 and size, the
 *          variant, the importer version, the verbatim header, the header
 *          fields the tag info and config do not hold, the report, and the
 *          check's result.
 *
 *          Exit status: 0 converted and checked (variant A: translated); 1
 *          failed; 2 not a log.
 *
 * @see     host/libraries/tagcore/design/proposals/retire-text-logs.md,
 *          host/libraries/tagcore/txtlogs.h, host/libraries/tagcore/sqlitelog.h
 */

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <google/protobuf/util/json_util.h>
#include <google/protobuf/util/message_differencer.h>
#include <cxxopts.hpp>
#include <sqlite3.h>

extern "C" {
#include "log.h"
}
#include "recovery/sha256.h"
#include "sqlitelog.h"
#include "txtlogs.h"

namespace {

namespace pbu = google::protobuf::util;

/// Version recorded in every converted log; raise it when output changes.
constexpr const char *kImporterVersion = "tag-txt-import 1";

/// Earliest plausible record time: 2015-01-01T00:00:00Z.
constexpr int64_t kEarliestEpoch = 1420070400;

/// Exit status for a file that is not convertible (variant A, not a log).
constexpr int kExitUnsupported = 2;

// ---------------------------------------------------------------------------
// Source text
// ---------------------------------------------------------------------------

/**
 * @struct  SourceFile
 * @brief   The input, split into lines with line endings normalised.
 */
struct SourceFile {
  std::string path;                ///< As given on the command line.
  std::string bytes;               ///< The file's exact contents.
  std::vector<std::string> lines;  ///< Lines without '\n' or a trailing '\r'.
  size_t crlf_lines = 0;           ///< Lines that ended in "\r\n".
  bool missing_final_newline = false; ///< The last line had no '\n'.
};

/**
 * @brief   Read a file and split it into lines.
 * @param[in]  path   File to read.
 * @param[out] out    The file.
 * @param[out] error  Why it failed.
 * @return  false when the file cannot be read.
 */
bool ReadSource(const std::string &path, SourceFile &out, std::string &error) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    error = "cannot read " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  out.path = path;
  out.bytes = buffer.str();
  size_t start = 0;
  while (start < out.bytes.size()) {
    size_t end = out.bytes.find('\n', start);
    const bool has_newline = end != std::string::npos;
    if (!has_newline)
      end = out.bytes.size();
    std::string line = out.bytes.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
      if (has_newline)
        out.crlf_lines++;
    }
    out.lines.push_back(line);
    if (!has_newline)
      out.missing_final_newline = true;
    start = end + 1;
  }
  return true;
}

/// Strip leading and trailing spaces and tabs.
std::string Trim(const std::string &s) {
  const size_t a = s.find_first_not_of(" \t");
  if (a == std::string::npos)
    return "";
  const size_t b = s.find_last_not_of(" \t");
  return s.substr(a, b - a + 1);
}

/// Split on a single character.
std::vector<std::string> Split(const std::string &s, char sep) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (true) {
    const size_t end = s.find(sep, start);
    parts.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos)
      return parts;
    start = end + 1;
  }
}

/// Parse a whole string as a signed integer.
bool ParseInt(const std::string &s, int64_t &out) {
  const std::string t = Trim(s);
  if (t.empty())
    return false;
  char *end = nullptr;
  errno = 0;
  const long long v = std::strtoll(t.c_str(), &end, 10);
  if (errno || *end)
    return false;
  out = v;
  return true;
}

/// Parse a whole string as a finite double.
bool ParseDouble(const std::string &s, double &out) {
  const std::string t = Trim(s);
  if (t.empty())
    return false;
  char *end = nullptr;
  errno = 0;
  const double v = std::strtod(t.c_str(), &end);
  if (errno || *end || !std::isfinite(v))
    return false;
  out = v;
  return true;
}

/// Parse a number followed by a unit suffix such as "KB", "g" or "s".
bool ParseWithSuffix(const std::string &s, const std::string &suffix, double &out) {
  std::string t = Trim(s);
  if (t.size() >= suffix.size() && t.compare(t.size() - suffix.size(), suffix.size(), suffix) == 0)
    t = t.substr(0, t.size() - suffix.size());
  return ParseDouble(t, out);
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

/**
 * @struct  ParsedHeader
 * @brief   A text header's fields, as the text writer's structures.
 */
struct ParsedHeader {
  TextLogHeader content;              ///< What TextTagLogWriter prints.
  bool has_tag_type = false;          ///< "Tag Type:" was present.
  bool has_board = false;             ///< "Board:" was present.
  bool has_read_time = false;         ///< "Tag Read time:" was present.
  bool has_bittag_format = false;     ///< A log-format line was present.
  std::string test_status_text;       ///< "Test Status:" verbatim.
  std::vector<std::string> unparsed;  ///< "key: value" lines not mapped.
  bool variant_a_marker = false;      ///< An RTCv4-style "# Time ... epoch:" line.
};

/**
 * @brief   Map one "key: value" header line onto the header structures.
 *
 * @param[in]     key      Text before the first ':' (trimmed).
 * @param[in]     value    Text after it (trimmed).
 * @param[in]     in_states  The line is in the state-log section.
 * @param[in,out] h        The header being built.
 * @return  false when the key is known but its value does not parse.
 */
bool MapHeaderField(const std::string &key, const std::string &value, bool in_states,
                    ParsedHeader &h) {
  TextLogHeader &c = h.content;
  double d = 0;
  int64_t n = 0;

  if (in_states) {
    if (key == "Epoch") {
      if (!ParseInt(value, n))
        return false;
      State state;
      state.mutable_status()->set_millis(n * 1000);
      c.states.push_back(state);
      return true;
    }
    if (c.states.empty())
      return false;
    Status *status = c.states.back().mutable_status();
    if (key == "State") {
      // Some firmware printed an empty value; it stays unspecified.
      TagState s;
      if (!value.empty() && !TagState_Parse(value, &s))
        return false;
      if (!value.empty())
        status->set_state(s);
    } else if (key == "Entry Code") {
      State_Event e;
      if (!value.empty() && !State_Event_Parse(value, &e))
        return false;
      if (!value.empty())
        c.states.back().set_transition_reason(e);
    } else if (key == "Temperature") {
      if (!ParseDouble(value, d))
        return false;
      status->set_temperature(static_cast<float>(d));
    } else if (key == "Voltage") {
      if (!ParseDouble(value, d))
        return false;
      status->set_voltage(static_cast<float>(d));
    } else if (key == "Internal Log Entries" || key == "Log Size") {
      if (!ParseInt(value, n))
        return false;
      status->set_internal_data_count(static_cast<int32_t>(n));
    } else if (key == "External Log Entries") {
      if (!ParseInt(value, n))
        return false;
      status->set_external_data_count(static_cast<int32_t>(n));
    } else {
      h.unparsed.push_back(key + ": " + value);
    }
    return true;
  }

  if (key == "Tag Read time") {
    // The V5 header prints a date before the epoch; the epoch is last.
    const std::vector<std::string> words = Split(Trim(value), ' ');
    if (!ParseInt(words.back(), n))
      return false;
    c.read_time_s = n;
    h.has_read_time = true;
  } else if (key == "Tag Error") {
    if (!ParseDouble(value, d))
      return false;
    c.error_s = d;
  } else if (key == "Tag Type") {
    TagType t;
    if (!TagType_Parse(value, &t))
      return false;
    c.info.set_tag_type(t);
    c.config.set_tag_type(t);
    h.has_tag_type = true;
  } else if (key == "Tag Firmware") {
    c.info.set_firmware(value);
  } else if (key == "Board") {
    c.info.set_board_desc(value);
    h.has_board = true;
  } else if (key == "Build Date") {
    c.info.set_build_time(value);
  } else if (key == "Git Repo") {
    c.info.set_gitrepo(value);
  } else if (key == "Source Path") {
    c.info.set_source_path(value);
  } else if (key == "Git Hash") {
    c.info.set_githash(value);
  } else if (key == "UUID") {
    c.info.set_uuid(value);
  } else if (key == "Internal Flash Size" || key == "Flash Size") {
    if (!ParseWithSuffix(value, "KB", d))
      return false;
    c.info.set_intflashsz(static_cast<int64_t>(d));
  } else if (key == "External Flash Size") {
    if (!ParseWithSuffix(value, "KB", d))
      return false;
    c.info.set_extflashsz(static_cast<int64_t>(d));
  } else if (key == "Test Status") {
    h.test_status_text = value;
    TestResult r;
    // Older firmware printed free text ("ADXL Test not run"); it is kept
    // verbatim and the structured field stays unspecified.
    if (TestResult_Parse(value, &r))
      c.test_status = r;
  } else if (key == "Range") {
    Adxl362_Rng r;
    if (!Adxl362_Rng_Parse(value, &r))
      return false;
    c.config.mutable_adxl362()->set_range(r);
  } else if (key == "Frequency") {
    Adxl362_Odr r;
    if (!Adxl362_Odr_Parse(value, &r))
      return false;
    c.config.mutable_adxl362()->set_freq(r);
  } else if (key == "Filter") {
    Adxl362_Aa r;
    if (!Adxl362_Aa_Parse(value, &r))
      return false;
    c.config.mutable_adxl362()->set_filter(r);
  } else if (key == "Active Thresh" || key == "Thresh") {
    if (!ParseWithSuffix(value, "g", d))
      return false;
    c.config.mutable_adxl362()->set_act_thresh_g(static_cast<float>(d));
  } else if (key == "Inactive Thresh") {
    if (!ParseWithSuffix(value, "g", d))
      return false;
    c.config.mutable_adxl362()->set_inact_thresh_g(static_cast<float>(d));
  } else if (key == "Inactive Time" || key == "thresh Time") {
    if (!ParseWithSuffix(value, "s", d))
      return false;
    c.config.mutable_adxl362()->set_inactive_sec(static_cast<float>(d));
  } else if (key == "Start") {
    if (!ParseInt(value, n))
      return false;
    c.config.mutable_active_interval()->set_start_epoch(static_cast<int32_t>(n));
  } else if (key == "End") {
    if (!ParseInt(value, n))
      return false;
    c.config.mutable_active_interval()->set_end_epoch(static_cast<int32_t>(n));
  } else if (key == "BitTag Log Format" || key == "Format") {
    // Current writers print the enum name; older ones its number, or the
    // name without the BITTAG_ prefix ("BITSPERFIVEMIN").
    BitTagLogFmt f;
    if (ParseInt(value, n) && BitTagLogFmt_IsValid(static_cast<int>(n)))
      f = static_cast<BitTagLogFmt>(n);
    else if (!BitTagLogFmt_Parse(value, &f) && !BitTagLogFmt_Parse("BITTAG_" + value, &f))
      return false;
    c.config.set_bittag_log(f);
    h.has_bittag_format = true;
  } else if (key == "Sample Period") {
    if (!ParseWithSuffix(value, "seconds", d))
      return false;
    c.config.set_period(static_cast<uint32_t>(d));
  } else {
    h.unparsed.push_back(key + ": " + value);
  }
  return true;
}

/**
 * @brief   Parse the leading '#' lines of a text log.
 *
 * @param[in]  lines   The header lines, in order.
 * @param[out] h       The fields.
 * @param[out] error   The first line that does not parse.
 * @return  false when a known field's value does not parse.
 */
bool ParseHeaderLines(const std::vector<std::string> &lines, ParsedHeader &h,
                      std::string &error) {
  bool in_states = false;
  for (const std::string &raw : lines) {
    std::string line = Trim(raw.substr(1));
    if (line.empty())
      continue;
    if (line == "State Log") {
      in_states = true;
      continue;
    }
    if (line == "Data Log" || line == "Tag Info" || line == "Configuration") {
      in_states = false;
      continue;
    }
    if (line.rfind("Hibernate[", 0) == 0) {
      // "Hibernate[i] = {start, end}"
      const size_t open = line.find('{');
      const size_t close = line.find('}');
      int64_t a = 0;
      int64_t b = 0;
      if (open == std::string::npos || close == std::string::npos)
        return error = "header: " + raw, false;
      const std::vector<std::string> pair =
          Split(line.substr(open + 1, close - open - 1), ',');
      if (pair.size() != 2 || !ParseInt(pair[0], a) || !ParseInt(pair[1], b))
        return error = "header: " + raw, false;
      Config::Interval *interval = h.content.config.add_hibernate();
      interval->set_start_epoch(static_cast<int32_t>(a));
      interval->set_end_epoch(static_cast<int32_t>(b));
      continue;
    }
    if (line.rfind("Time", 0) == 0 && line.find("epoch:") != std::string::npos) {
      h.variant_a_marker = true;
      continue;
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      h.unparsed.push_back(line);
      continue;
    }
    if (!MapHeaderField(Trim(line.substr(0, colon)), Trim(line.substr(colon + 1)), in_states, h)) {
      error = "header: " + raw;
      return false;
    }
  }
  // The writer prints "Start: 0" and "End: 0" for an unset interval, so an
  // all-zero interval and an absent one are the same header.
  const Config::Interval &interval = h.content.config.active_interval();
  if (interval.start_epoch() == 0 && interval.end_epoch() == 0)
    h.content.config.clear_active_interval();
  return true;
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

/**
 * @struct  DataLine
 * @brief   One data line, split into its time and fields.
 */
struct DataLine {
  size_t number = 0;               ///< 1-based line number in the file.
  std::string text;                ///< The line as normalised.
  int64_t epoch = 0;               ///< Leading Unix time.
  std::vector<std::string> fields; ///< Comma-separated fields after the time.
  bool after_comment = false;      ///< A '#' line came between this and the last data line.
};

/**
 * @struct  BitTagRecord
 * @brief   One BitTag record: its activity lines and the `V:` line closing it.
 */
struct BitTagRecord {
  std::vector<size_t> lines;   ///< Indexes into the data lines, in order.
  int64_t epoch = 0;           ///< The `V:` line's time, the record's end.
  std::string key;             ///< Activity key: MIN, FOURMIN or FIVEMIN.
  std::vector<int64_t> bucket_epochs;
  std::vector<double> pct;     ///< Activity as printed.
  std::vector<std::string> pct_text; ///< The printed values, for their precision.
  uint64_t raw = 0;            ///< The stored word, in the correct layout.
  double voltage = 0;
  double core_temperature = 0;
  bool has_word = false;       ///< C3/C4: a fourth field on the `V:` line.
  uint64_t word = 0;
};

/**
 * @struct  PresTagRecord
 * @brief   One PresTag block: a `V:` line and its pressure/temperature pairs.
 */
struct PresTagRecord {
  std::vector<size_t> lines;
  int64_t epoch = 0;
  double voltage = 0;
  double core_temperature = 0;
  std::vector<double> pressure;
  std::vector<double> temperature;
};

/**
 * @struct  Geometry
 * @brief   How a BitTag format packs activity into one stored word.
 */
struct Geometry {
  BitTagLogFmt format = BITTAG_UNSPECIFIED;
  int bits = 0;    ///< Bits per bucket.
  int number = 0;  ///< Buckets per record.
  int period = 0;  ///< Seconds per bucket.
};

/// The correct geometry for a format, as both writers decode it.
bool GeometryFor(BitTagLogFmt format, Geometry &g) {
  switch (format) {
  case BITTAG_BITSPERMIN: g = {format, 6, 10, 60}; return true;
  case BITTAG_BITSPERFOURMIN: g = {format, 8, 8, 240}; return true;
  case BITTAG_BITSPERFIVEMIN: g = {format, 9, 7, 300}; return true;
  default: return false;
  }
}

/// The pre-2021 host's FIVEMIN decode: nine 7-bit buckets.
const Geometry kBuggyFiveMin = {BITTAG_BITSPERFIVEMIN, 7, 9, 300};

/// The activity key a format prints, as TextTagLogWriter does.
const char *KeyFor(BitTagLogFmt format) {
  switch (format) {
  case BITTAG_BITSPERMIN: return "MIN";
  case BITTAG_BITSPERFOURMIN: return "FOURMIN";
  case BITTAG_BITSPERFIVEMIN: return "FIVEMIN";
  default: return "";
  }
}

/**
 * @struct  Report
 * @brief   What the conversion found and did.
 */
struct Report {
  std::string variant;
  std::string tag_type;
  size_t data_lines = 0;
  size_t records_in = 0;
  size_t records_written = 0;
  size_t exact_duplicates = 0;      ///< Removed.
  size_t conflicting_duplicates = 0; ///< Kept.
  size_t implausible_times = 0;     ///< Dropped.
  size_t backward_jumps = 0;        ///< Kept, file order.
  size_t voltage_pairs_averaged = 0; ///< PresTag: V: and TC: stored as their mean.
  size_t bit30_words = 0;           ///< Variant A: words with bit 30 set (dropped).
  size_t counts_over_60 = 0;        ///< Variant A: per-minute counts above 60 s (kept).
  std::vector<std::string> data_comments; ///< '#' lines after the data began.
  size_t crlf_lines = 0;
  bool missing_final_newline = false;
  bool header_only = false;
  bool fivemin_9x7 = false;         ///< Re-decoded from the buggy layout.
  bool read_time_implausible = false; ///< The tag's clock was wrong when read.
  size_t words_checked = 0;         ///< C3/C4 fourth fields checked.
  std::vector<std::string> unparsed_header;
  std::vector<std::string> dropped_lines; ///< Verbatim, "line N: text".
  std::string roundtrip;            ///< "passed" or why not.
  size_t roundtrip_exact = 0;       ///< Data lines equal byte for byte.
  size_t roundtrip_within_precision = 0;
};

/**
 * @brief   Replace bytes that are not valid UTF-8 with '?'.
 * @details A file that is not a log (a binary AppleDouble file, say) can put
 *          arbitrary bytes into an error message; the report must stay JSON.
 * @param[in] s  Text of unknown encoding.
 * @return  Valid UTF-8.
 */
std::string SanitizeUtf8(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    bool valid = n > 0 && i + n <= s.size();
    for (size_t k = 1; valid && k < n; k++)
      valid = (static_cast<unsigned char>(s[i + k]) >> 6) == 0x2;
    if (valid) {
      out.append(s, i, n);
      i += n;
    } else {
      out += '?';
      i++;
    }
  }
  return out;
}

/// The report as JSON, for the info table and --report.
std::string ReportJson(const Report &r) {
  std::ostringstream o;
  auto str = [&](const std::string &raw) {
    const std::string s = SanitizeUtf8(raw);
    o << '"';
    for (char ch : s) {
      if (ch == '"' || ch == '\\')
        o << '\\' << ch;
      else if (static_cast<unsigned char>(ch) < 0x20)
        o << ' ';
      else
        o << ch;
    }
    o << '"';
  };
  auto list = [&](const std::vector<std::string> &v) {
    o << '[';
    for (size_t i = 0; i < v.size(); i++) {
      if (i)
        o << ", ";
      str(v[i]);
    }
    o << ']';
  };
  o << "{\n  \"importer\": ";
  str(kImporterVersion);
  o << ",\n  \"variant\": ";
  str(r.variant);
  o << ",\n  \"tag_type\": ";
  str(r.tag_type);
  o << ",\n  \"data_lines\": " << r.data_lines << ",\n  \"records_in\": " << r.records_in
    << ",\n  \"records_written\": " << r.records_written
    << ",\n  \"exact_duplicates_removed\": " << r.exact_duplicates
    << ",\n  \"conflicting_duplicates_kept\": " << r.conflicting_duplicates
    << ",\n  \"implausible_times_dropped\": " << r.implausible_times
    << ",\n  \"backward_time_jumps\": " << r.backward_jumps
    << ",\n  \"prestag_voltage_pairs_averaged\": " << r.voltage_pairs_averaged
    << ",\n  \"variant_a_bit30_words_dropped\": " << r.bit30_words
    << ",\n  \"variant_a_counts_over_60\": " << r.counts_over_60
    << ",\n  \"crlf_lines\": " << r.crlf_lines
    << ",\n  \"missing_final_newline\": " << (r.missing_final_newline ? "true" : "false")
    << ",\n  \"header_only\": " << (r.header_only ? "true" : "false")
    << ",\n  \"repair_fivemin_9x7\": " << (r.fivemin_9x7 ? "true" : "false")
    << ",\n  \"read_time_implausible\": " << (r.read_time_implausible ? "true" : "false")
    << ",\n  \"packed_words_checked\": " << r.words_checked << ",\n  \"unparsed_header\": ";
  list(r.unparsed_header);
  o << ",\n  \"dropped_lines\": ";
  list(r.dropped_lines);
  o << ",\n  \"data_comments\": ";
  list(r.data_comments);
  o << ",\n  \"roundtrip\": ";
  str(r.roundtrip);
  o << ",\n  \"roundtrip_exact_lines\": " << r.roundtrip_exact
    << ",\n  \"roundtrip_within_precision_lines\": " << r.roundtrip_within_precision << "\n}";
  return o.str();
}

/**
 * @brief   Split the data section into lines with a leading epoch.
 *
 * @param[in]  src     The file.
 * @param[in]  first   Index of the first data line.
 * @param[out] out     The lines; blank lines are skipped.
 * @param[out] comments  '#' lines found among the data, verbatim.
 * @param[out] error   The first line without a leading integer time.
 * @return  false on a malformed line.
 */
bool SplitData(const SourceFile &src, size_t first, std::vector<DataLine> &out,
               std::vector<std::string> &comments, std::string &error) {
  bool pending_comment = false;
  for (size_t i = first; i < src.lines.size(); i++) {
    const std::string &text = src.lines[i];
    if (Trim(text).empty())
      continue;
    // Some exports end with "# End of File", and some files are two exports
    // run together. A comment is kept in the report and ends any record.
    if (text[0] == '#') {
      comments.push_back("line " + std::to_string(i + 1) + ": " + text);
      pending_comment = true;
      continue;
    }
    std::vector<std::string> parts = Split(text, ',');
    DataLine line;
    line.number = i + 1;
    line.text = text;
    if (parts.size() < 2 || !ParseInt(parts[0], line.epoch)) {
      error = "line " + std::to_string(i + 1) + ": not a data line: " + text;
      return false;
    }
    line.fields.assign(parts.begin() + 1, parts.end());
    line.after_comment = pending_comment;
    pending_comment = false;
    out.push_back(line);
  }
  return true;
}

/// Split "KEY:value" into its parts.
bool KeyValue(const std::string &field, std::string &key, std::string &value) {
  const size_t colon = field.find(':');
  if (colon == std::string::npos)
    return false;
  key = field.substr(0, colon);
  value = field.substr(colon + 1);
  return true;
}

/**
 * @brief   Group BitTag data lines into records.
 *
 * @details Activity lines accumulate until a `V:` line closes the record.
 *          A `V:` line with nothing to close, or activity that no `V:` line
 *          closes before a comment or the end of the file, is an error: it is
 *          where exports were run together, and which records the lines
 *          belong to cannot be told from the file.
 *
 * @param[in]     data     The data lines.
 * @param[out]    records  The records, in file order.
 * @param[out]    error    Why the data does not parse.
 * @return  false on a line that is neither activity nor `V:`, or on an
 *          orphan or unclosed record.
 */
bool GroupBitTag(const std::vector<DataLine> &data, std::vector<BitTagRecord> &records,
                 std::string &error) {
  BitTagRecord current;
  auto unclosed = [&]() {
    if (current.lines.empty())
      return false;
    error = "line " + std::to_string(data[current.lines.front()].number) +
            ": activity that no V: line closes (exports run together?)";
    return true;
  };
  for (size_t i = 0; i < data.size(); i++) {
    const DataLine &line = data[i];
    // A comment ends an export: activity it left unclosed is an error.
    if (line.after_comment && unclosed())
      return false;
    std::string key;
    std::string value;
    if (!KeyValue(line.fields[0], key, value)) {
      error = "line " + std::to_string(line.number) + ": " + line.text;
      return false;
    }
    if (key == "V") {
      std::string tc_key;
      std::string tc_value;
      double v = 0;
      double tc = 0;
      if (line.fields.size() < 2 || line.fields.size() > 3 || !ParseDouble(value, v) ||
          !KeyValue(line.fields[1], tc_key, tc_value) || tc_key != "TC" ||
          !ParseDouble(tc_value, tc)) {
        error = "line " + std::to_string(line.number) + ": bad V: line: " + line.text;
        return false;
      }
      if (current.lines.empty()) {
        error = "line " + std::to_string(line.number) +
                ": a V: line with no activity before it (exports run together?)";
        return false;
      }
      current.lines.push_back(i);
      current.epoch = line.epoch;
      current.voltage = v;
      current.core_temperature = tc;
      if (line.fields.size() == 3) {
        int64_t word = 0;
        if (!ParseInt(line.fields[2], word) || word < 0) {
          error = "line " + std::to_string(line.number) + ": bad packed word: " + line.text;
          return false;
        }
        current.has_word = true;
        current.word = static_cast<uint64_t>(word);
      }
      records.push_back(current);
      current = BitTagRecord();
      continue;
    }
    double pct = 0;
    if (key != "MIN" && key != "FOURMIN" && key != "FIVEMIN") {
      error = "line " + std::to_string(line.number) + ": unexpected key " + key;
      return false;
    }
    if (line.fields.size() != 1 || !ParseDouble(value, pct)) {
      error = "line " + std::to_string(line.number) + ": bad activity line: " + line.text;
      return false;
    }
    if (!current.key.empty() && current.key != key) {
      error = "line " + std::to_string(line.number) + ": activity key changes within a record";
      return false;
    }
    current.key = key;
    current.lines.push_back(i);
    current.bucket_epochs.push_back(line.epoch);
    current.pct.push_back(pct);
    current.pct_text.push_back(Trim(value));
  }
  return !unclosed();
}

int Decimals(const std::string &token);

/**
 * @brief   Check one record against a geometry and pack its word.
 *
 * @param[in]  r      The record.
 * @param[in]  g      The layout the file was written with.
 * @param[out] raw    The bucket counts packed in that layout.
 * @param[out] error  Why the record does not fit.
 * @return  false when the bucket count, times or values do not fit.
 */
bool PackRecord(const BitTagRecord &r, const Geometry &g, uint64_t &raw, std::string &error) {
  if (static_cast<int>(r.pct.size()) != g.number) {
    error = "record ending " + std::to_string(r.epoch) + " has " + std::to_string(r.pct.size()) +
            " buckets, expected " + std::to_string(g.number);
    return false;
  }
  raw = 0;
  for (int i = 0; i < g.number; i++) {
    if (r.bucket_epochs[i] != r.epoch - static_cast<int64_t>(g.period) * (g.number - 1 - i)) {
      error = "record ending " + std::to_string(r.epoch) + ": bucket times do not fit the layout";
      return false;
    }
    const double scaled = r.pct[i] * g.period / 100.0;
    const long long count = std::llround(scaled);
    // The value is a whole count printed to some precision: half a unit of
    // its last printed digit, in counts, is as far off as it can be.
    const int d = std::max(0, Decimals(r.pct_text[i]));
    const double tolerance = std::max(0.02, 0.5 * std::pow(10.0, -d) * g.period / 100.0 + 1e-9);
    if (std::fabs(scaled - static_cast<double>(count)) > tolerance || count < 0 ||
        count >= (1LL << g.bits)) {
      error = "record ending " + std::to_string(r.epoch) + ": activity " +
              std::to_string(r.pct[i]) + " is not a whole count";
      return false;
    }
    raw |= static_cast<uint64_t>(count) << (i * g.bits);
  }
  return true;
}

/// Unpack a word into bucket counts.
std::vector<uint64_t> Unpack(uint64_t raw, const Geometry &g) {
  std::vector<uint64_t> counts;
  for (int i = 0; i < g.number; i++)
    counts.push_back((raw >> (i * g.bits)) & ((1ULL << g.bits) - 1));
  return counts;
}

/**
 * @brief   Group PresTag data lines into blocks.
 *
 * @param[in]  data     The data lines.
 * @param[in]  period   Seconds between samples; 0 to infer from the data.
 * @param[out] records  The blocks.
 * @param[out] inferred The period used.
 * @param[out] error    Why the data does not parse.
 * @return  false on a malformed line or sample times that do not fit.
 */
bool GroupPresTag(const std::vector<DataLine> &data, uint32_t period,
                  std::vector<PresTagRecord> &records, uint32_t &inferred, std::string &error) {
  PresTagRecord current;
  bool open = false;
  for (size_t i = 0; i < data.size(); i++) {
    const DataLine &line = data[i];
    std::string key;
    std::string value;
    double v = 0;
    if (!KeyValue(line.fields[0], key, value) || !ParseDouble(value, v)) {
      error = "line " + std::to_string(line.number) + ": " + line.text;
      return false;
    }
    if (key == "V") {
      std::string tc_key;
      std::string tc_value;
      double tc = 0;
      if (line.fields.size() != 2 || !KeyValue(line.fields[1], tc_key, tc_value) ||
          tc_key != "TC" || !ParseDouble(tc_value, tc)) {
        error = "line " + std::to_string(line.number) + ": bad V: line: " + line.text;
        return false;
      }
      if (open)
        records.push_back(current);
      current = PresTagRecord();
      current.lines.push_back(i);
      current.epoch = line.epoch;
      current.voltage = v;
      current.core_temperature = tc;
      open = true;
      continue;
    }
    if (!open || line.fields.size() != 1 || (key != "P" && key != "T")) {
      error = "line " + std::to_string(line.number) + ": unexpected line: " + line.text;
      return false;
    }
    current.lines.push_back(i);
    if (key == "P") {
      current.pressure.push_back(v);
    } else {
      if (current.temperature.size() + 1 != current.pressure.size()) {
        error = "line " + std::to_string(line.number) + ": T: without its P:";
        return false;
      }
      current.temperature.push_back(v);
    }
  }
  if (open)
    records.push_back(current);

  // The period: the header's, or the first spacing in the data.
  inferred = period;
  for (const PresTagRecord &r : records) {
    if (inferred == 0 && r.lines.size() >= 5)
      inferred = static_cast<uint32_t>(data[r.lines[3]].epoch - data[r.lines[1]].epoch);
  }
  for (const PresTagRecord &r : records) {
    if (r.pressure.size() != r.temperature.size()) {
      error = "block at " + std::to_string(r.epoch) + ": P: without its T:";
      return false;
    }
    for (size_t k = 1; k < r.lines.size(); k++) {
      const int64_t expected = r.epoch + static_cast<int64_t>(inferred) * ((k - 1) / 2);
      if (data[r.lines[k]].epoch != expected) {
        error = "line " + std::to_string(data[r.lines[k]].number) +
                ": sample time does not fit the block's period";
        return false;
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

/// The parsed file, ready to write.
struct Conversion {
  ParsedHeader header;
  std::string header_text;        ///< The header lines, verbatim.
  std::vector<DataLine> data;
  Geometry geometry;              ///< BitTag: the correct layout.
  bool buggy = false;             ///< BitTag: written in the 9×7 layout.
  bool has_words = false;         ///< BitTag: C3/C4 fourth fields.
  std::vector<BitTagRecord> bittag;
  std::vector<PresTagRecord> prestag;
  bool variant_a = false;         ///< Translated with btviz's decode; no round trip.
  std::vector<TagLogSample> a_activity, a_voltage, a_core; ///< Variant A rows.
  std::string a_device;           ///< Variant A "# device:" string.
  std::set<size_t> dropped;       ///< Data-line indexes removed by repairs.
  Report report;
};

/**
 * @brief   Remove duplicate and implausible records, recording each.
 *
 * @tparam  R  BitTagRecord or PresTagRecord.
 * @param[in,out] records  Records in file order; repaired in place.
 * @param[in]     same     Whether two records with one time are identical.
 * @param[in]     latest   Latest plausible time.
 * @param[in,out] c        The conversion: report and dropped lines.
 */
template <typename R, typename Same>
void Repair(std::vector<R> &records, Same same, int64_t latest, Conversion &c) {
  std::vector<R> kept;
  std::map<int64_t, std::vector<size_t>> by_epoch; // epoch -> indexes into kept
  int64_t last = INT64_MIN;
  auto drop = [&](const R &r) {
    for (size_t index : r.lines) {
      c.dropped.insert(index);
      c.report.dropped_lines.push_back("line " + std::to_string(c.data[index].number) + ": " +
                                       c.data[index].text);
    }
  };
  for (const R &r : records) {
    if (r.epoch < kEarliestEpoch || r.epoch > latest) {
      c.report.implausible_times++;
      drop(r);
      continue;
    }
    bool duplicate = false;
    bool conflict = false;
    for (size_t k : by_epoch[r.epoch]) {
      if (same(kept[k], r))
        duplicate = true;
      else
        conflict = true;
    }
    if (duplicate) {
      c.report.exact_duplicates++;
      drop(r);
      continue;
    }
    if (conflict)
      c.report.conflicting_duplicates++;
    if (r.epoch < last)
      c.report.backward_jumps++;
    last = r.epoch;
    by_epoch[r.epoch].push_back(kept.size());
    kept.push_back(r);
  }
  records = kept;
}

/**
 * @brief   Write the conversion into a SQLite log.
 *
 * @param[in]  c        The parsed, repaired file.
 * @param[in]  src      The source, for provenance.
 * @param[in]  rel      The source path to record.
 * @param[in]  db_path  Output, replaced.
 * @param[out] writer   The open writer, for the check's result afterwards.
 * @param[out] error    Why it failed.
 * @return  false on a writer error.
 */
bool WriteLog(const Conversion &c, const SourceFile &src, const std::string &rel,
              const std::string &db_path, std::unique_ptr<SqliteTagLogWriter> &writer,
              std::string &error) {
  const TextLogHeader &h = c.header.content;
  writer = std::make_unique<SqliteTagLogWriter>(db_path, h.config, true);
  TagLogHeader header;
  header.config = h.config;
  header.info = h.info;
  header.states = h.states;
  if (!writer->isOpen() || !writer->writeHeader(header)) {
    error = writer->lastError();
    return false;
  }
  const std::vector<std::pair<std::string, std::string>> provenance = {
      {"source", "txt-import"},
      {"txt_source_path", rel},
      {"txt_source_sha256",
       tagcore::recovery::Sha256::Of(reinterpret_cast<const uint8_t *>(src.bytes.data()),
                                     src.bytes.size())},
      {"txt_source_size", std::to_string(src.bytes.size())},
      {"txt_variant", c.report.variant},
      {"txt_importer", kImporterVersion},
      {"txt_header", c.header_text},
      {"txt_read_time", c.header.has_read_time ? std::to_string(h.read_time_s) : ""},
      {"txt_tag_error", [&] {
         std::ostringstream o;
         o << h.error_s;
         return o.str();
       }()},
      {"txt_test_status", c.header.test_status_text},
  };
  for (const auto &[key, value] : provenance) {
    if (!writer->writeInfo(key, value)) {
      error = writer->lastError();
      return false;
    }
  }

  std::vector<TagLogSample> activity, voltage, core, pressure, temperature;
  if (c.variant_a) {
    activity = c.a_activity;
    voltage = c.a_voltage;
    core = c.a_core;
    if (!writer->writeInfo("txt_device", c.a_device) ||
        !writer->writeInfo("txt_translation",
                           "variant A, decoded as btviz decodes it; not round-trip checked")) {
      error = writer->lastError();
      return false;
    }
  } else if (!c.bittag.empty() || h.config.tag_type() == BITTAG) {
    for (const BitTagRecord &r : c.bittag) {
      const std::vector<uint64_t> counts = Unpack(r.raw, c.geometry);
      for (int i = 0; i < c.geometry.number; i++)
        activity.push_back({r.epoch - static_cast<int64_t>(c.geometry.period) *
                                          (c.geometry.number - 1 - i),
                            counts[i] * 100.0 / c.geometry.period});
      voltage.push_back({r.epoch, r.voltage});
      core.push_back({r.epoch, r.core_temperature});
    }
  }
  for (const PresTagRecord &r : c.prestag) {
    // TC: is a second voltage on PresTag: keep one, their mean.
    voltage.push_back({r.epoch, (r.voltage + r.core_temperature) / 2.0});
    for (size_t k = 0; k < r.pressure.size(); k++) {
      const int64_t t = r.epoch + static_cast<int64_t>(h.config.period()) * k;
      pressure.push_back({t, r.pressure[k]});
      temperature.push_back({t, r.temperature[k]});
    }
  }
  bool ok = writer->beginLog() && writer->writeSamples(TagLogSeries::Voltage, voltage);
  if (ok && h.config.tag_type() != PRESTAG)
    ok = writer->writeSamples(TagLogSeries::CoreTemperature, core);
  if (ok && h.config.tag_type() == PRESTAG)
    ok = writer->writeSamples(TagLogSeries::Pressure, pressure) &&
         writer->writeSamples(TagLogSeries::Temperature, temperature);
  else if (ok)
    ok = writer->writeSamples(TagLogSeries::Activity, activity);
  if (!ok || !writer->endLog()) {
    error = writer->lastError();
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

/// Rows of one value table, in rowid order.
bool ReadSeries(sqlite3 *db, const std::string &sql, std::vector<TagLogSample> &out) {
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  while (sqlite3_step(stmt) == SQLITE_ROW)
    out.push_back({sqlite3_column_int64(stmt, 0), sqlite3_column_double(stmt, 1)});
  sqlite3_finalize(stmt);
  return true;
}

/// One info value, or "" when absent.
std::string ReadInfo(sqlite3 *db, const std::string &key) {
  sqlite3_stmt *stmt = nullptr;
  std::string value;
  if (sqlite3_prepare_v2(db, "SELECT value FROM info WHERE fieldname = ?", -1, &stmt,
                         nullptr) == SQLITE_OK) {
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_text(stmt, 0))
      value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
  }
  sqlite3_finalize(stmt);
  return value;
}

/// Decimal places printed in a number token; -1 when it is not one.
int Decimals(const std::string &token) {
  double v = 0;
  if (!ParseDouble(token, v))
    return -1;
  const size_t dot = token.find('.');
  return dot == std::string::npos ? 0 : static_cast<int>(token.size() - dot - 1);
}

/**
 * @brief   Compare a rendered data line with the source's.
 *
 * @details Byte-for-byte equal, or the same epoch and keys with each number
 *          within half a unit of the source's last printed digit.
 *
 * @param[in]  original  Source line.
 * @param[in]  rendered  Rendered line.
 * @param[out] exact     Whether they were byte-for-byte equal.
 * @return  false when they differ beyond printed precision.
 */
bool SameLine(const std::string &original, const std::string &rendered, bool &exact) {
  exact = original == rendered;
  if (exact)
    return true;
  const std::vector<std::string> a = Split(original, ',');
  const std::vector<std::string> b = Split(rendered, ',');
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); i++) {
    std::string ka, va, kb, vb;
    const bool kv = KeyValue(a[i], ka, va);
    if (kv != KeyValue(b[i], kb, vb))
      return false;
    if (!kv) {
      va = a[i];
      vb = b[i];
    } else if (ka != kb) {
      return false;
    }
    if (va == vb)
      continue;
    // Each side is a value rounded to its own printed precision: the
    // writer's six significant digits, or an older writer's decimals.
    const int da = Decimals(va);
    const int db = Decimals(vb);
    double x = 0;
    double y = 0;
    if (da < 0 || db < 0 || i == 0 || !ParseDouble(va, x) || !ParseDouble(vb, y))
      return false;
    if (std::fabs(x - y) > 0.5 * (std::pow(10.0, -da) + std::pow(10.0, -db)) + 1e-9)
      return false;
  }
  return true;
}

/**
 * @brief   Compare a PresTag `V:` line, whose two voltages were stored as
 *          their mean, with the rendered line carrying that mean twice.
 *
 * @param[in] original  Source line, `epoch,V:a,TC:b`.
 * @param[in] rendered  Rendered line, `epoch,V:m,TC:m`.
 * @return  true when the epochs match, both rendered values are the same,
 *          and that value is the source pair's mean within printed precision.
 */
bool SameAveragedVoltageLine(const std::string &original, const std::string &rendered) {
  const std::vector<std::string> a = Split(original, ',');
  const std::vector<std::string> b = Split(rendered, ',');
  std::string k1, v1, k2, v2, k3, m1, k4, m2;
  double x = 0, y = 0, m = 0;
  if (a.size() != 3 || b.size() != 3 || a[0] != b[0] || !KeyValue(a[1], k1, v1) ||
      !KeyValue(a[2], k2, v2) || !KeyValue(b[1], k3, m1) || !KeyValue(b[2], k4, m2) ||
      k1 != "V" || k2 != "TC" || k3 != "V" || k4 != "TC" || m1 != m2 || !ParseDouble(v1, x) ||
      !ParseDouble(v2, y) || !ParseDouble(m1, m))
    return false;
  const int d = std::min(std::max(0, Decimals(v1)), std::max(0, Decimals(v2)));
  return std::fabs(m - (x + y) / 2.0) <=
         0.5 * (std::pow(10.0, -d) + std::pow(10.0, -std::max(0, Decimals(m1)))) + 1e-9;
}

/**
 * @brief   Render the written log back to text and compare with the source.
 *
 * @param[in]     db_path  The written log.
 * @param[in]     c        The conversion: the source lines and repairs.
 * @param[in,out] report   Line counts, and the result.
 * @return  true when every data line and header field matches.
 */
bool RoundTrip(const std::string &db_path, const Conversion &c, Report &report) {
  sqlite3 *db = nullptr;
  if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    report.roundtrip = "cannot reopen the written log";
    sqlite3_close(db);
    return false;
  }
  auto fail = [&](const std::string &why) {
    report.roundtrip = why;
    sqlite3_close(db);
    return false;
  };

  // The header, rebuilt only from what the log stores.
  TextLogHeader stored;
  if (!pbu::JsonStringToMessage(ReadInfo(db, "config"), &stored.config).ok() ||
      !pbu::JsonStringToMessage(ReadInfo(db, "info"), &stored.info).ok())
    return fail("cannot read config/info back");
  int64_t n = 0;
  if (ParseInt(ReadInfo(db, "txt_read_time"), n))
    stored.read_time_s = n;
  ParseDouble(ReadInfo(db, "txt_tag_error"), stored.error_s);
  TestResult result;
  if (TestResult_Parse(ReadInfo(db, "txt_test_status"), &result))
    stored.test_status = result;
  {
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db,
                           "SELECT Epoch, State, EntryCode, Temperature, Voltage, "
                           "InternalLogSize, ExternalLogSize FROM states ORDER BY rowid",
                           -1, &stmt, nullptr) != SQLITE_OK)
      return fail("cannot read states back");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      State state;
      Status *status = state.mutable_status();
      status->set_millis(sqlite3_column_int64(stmt, 0) * 1000);
      TagState s;
      State_Event e;
      TagState_Parse(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)), &s);
      State_Event_Parse(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)), &e);
      status->set_state(s);
      state.set_transition_reason(e);
      status->set_temperature(static_cast<float>(sqlite3_column_double(stmt, 3)));
      status->set_voltage(static_cast<float>(sqlite3_column_double(stmt, 4)));
      status->set_internal_data_count(sqlite3_column_int(stmt, 5));
      status->set_external_data_count(sqlite3_column_int(stmt, 6));
      stored.states.push_back(state);
    }
    sqlite3_finalize(stmt);
  }

  // Header: render the stored fields with the writer, parse that back, and
  // compare with what the source's header parsed to.
  {
    std::ostringstream text;
    TextTagLogWriter writer(text, stored.config);
    writer.writeHeader(stored);
    std::vector<std::string> lines;
    for (const std::string &line : Split(text.str(), '\n'))
      if (!line.empty())
        lines.push_back(line);
    ParsedHeader again;
    std::string error;
    if (!ParseHeaderLines(lines, again, error))
      return fail("rendered header does not parse: " + error);
    const TextLogHeader &a = c.header.content;
    const TextLogHeader &b = again.content;
    pbu::MessageDifferencer differ;
    std::string why;
    differ.ReportDifferencesToString(&why);
    if (!differ.Compare(a.info, b.info))
      return fail("header tag info differs: " + why);
    if (!differ.Compare(a.config, b.config))
      return fail("header config differs: " + why);
    if (a.states.size() != b.states.size())
      return fail("header state log differs in length");
    for (size_t i = 0; i < a.states.size(); i++)
      if (!differ.Compare(a.states[i], b.states[i]))
        return fail("header state " + std::to_string(i) + " differs: " + why);
    if (a.read_time_s != b.read_time_s || a.test_status != b.test_status ||
        std::fabs(a.error_s - b.error_s) > 1e-6 * std::max(1.0, std::fabs(a.error_s)))
      return fail("header read time, error or test status differs");
  }

  // Data: rebuild the records from the tables, in rowid order.
  std::vector<std::string> rendered;
  std::vector<TagLogSample> volts, cores;
  if (!ReadSeries(db, "SELECT Epoch, Voltage FROM Voltage ORDER BY rowid", volts))
    return fail("cannot read voltage back");
  if (stored.config.tag_type() == PRESTAG) {
    // One stored voltage stands for both V: and TC:.
    cores = volts;
  } else if (!ReadSeries(db, "SELECT Epoch, Temperature FROM CoreTemperature ORDER BY rowid",
                         cores) ||
             volts.size() != cores.size()) {
    return fail("voltage and core temperature rows do not pair");
  }

  if (stored.config.tag_type() == PRESTAG) {
    std::vector<TagLogSample> pres, temps;
    if (!ReadSeries(db, "SELECT Epoch, Pressure FROM Pressure ORDER BY rowid", pres) ||
        !ReadSeries(db, "SELECT Epoch, Temperature FROM Temperature ORDER BY rowid", temps) ||
        pres.size() != temps.size())
      return fail("pressure and temperature rows do not pair");
    std::ostringstream out;
    TextTagLogWriter writer(out, stored.config);
    size_t next = 0;
    for (size_t k = 0; k < volts.size(); k++) {
      Ack ack;
      PresTagLog *log = ack.mutable_prestag_data_log();
      log->set_epoch(static_cast<int32_t>(volts[k].epoch));
      log->set_voltage(static_cast<float>(volts[k].value));
      log->set_temperature(static_cast<float>(cores[k].value));
      // A block's samples are the next rows at its time plus whole periods,
      // so blocks sharing a time cannot take each other's samples.
      // A block also ends where the next one starts, when that is later.
      const int64_t limit = k + 1 < volts.size() && volts[k + 1].epoch > volts[k].epoch
                                ? volts[k + 1].epoch
                                : INT64_MAX;
      int64_t expected = volts[k].epoch;
      while (next < pres.size() && pres[next].epoch == expected && expected < limit) {
        expected += stored.config.period();
        PresTagLog::PT *pt = log->add_data();
        pt->set_pressure(static_cast<float>(pres[next].value));
        pt->set_temperature(static_cast<float>(temps[next].value));
        next++;
      }
      writer.writeLog(ack);
    }
    if (next != pres.size())
      return fail("pressure rows left over after rebuilding blocks");
    for (const std::string &line : Split(out.str(), '\n'))
      if (!line.empty())
        rendered.push_back(line);
  } else {
    std::vector<TagLogSample> acts;
    if (!ReadSeries(db, "SELECT Epoch, Activity FROM Activity ORDER BY rowid", acts))
      return fail("cannot read activity back");
    const Geometry &g = c.geometry;
    if (acts.size() != volts.size() * g.number)
      return fail("activity rows are not whole records");
    for (size_t k = 0; k < volts.size(); k++) {
      uint64_t raw = 0;
      for (int i = 0; i < g.number; i++) {
        const TagLogSample &s = acts[k * g.number + i];
        if (s.epoch != volts[k].epoch - static_cast<int64_t>(g.period) * (g.number - 1 - i))
          return fail("activity row times do not fit record " + std::to_string(k));
        raw |= static_cast<uint64_t>(std::llround(s.value * g.period / 100.0)) << (i * g.bits);
      }
      std::ostringstream out;
      if (c.buggy) {
        // The pre-2021 host's layout, rendered with the writer's formatting.
        const std::vector<uint64_t> counts = Unpack(raw, kBuggyFiveMin);
        for (int i = 0; i < kBuggyFiveMin.number; i++)
          out << volts[k].epoch - kBuggyFiveMin.period * (kBuggyFiveMin.number - 1 - i)
              << ",FIVEMIN:" << (counts[i] * 100.0 / kBuggyFiveMin.period) << std::endl;
        out << volts[k].epoch << ",V:" << static_cast<float>(volts[k].value)
            << ",TC:" << static_cast<float>(cores[k].value) << std::endl;
      } else {
        Ack ack;
        BitTagData *entry = ack.mutable_bittag_data_log()->add_data();
        entry->set_epoch(static_cast<int32_t>(volts[k].epoch));
        entry->set_voltage(static_cast<float>(volts[k].value));
        entry->set_temperature(static_cast<float>(cores[k].value));
        entry->set_rawdata(raw);
        TextTagLogWriter writer(out, stored.config);
        writer.writeLog(ack);
      }
      for (const std::string &line : Split(out.str(), '\n')) {
        if (line.empty())
          continue;
        // C3/C4: an older writer appended the record's packed word.
        if (c.has_words && line.find(",V:") != std::string::npos)
          rendered.push_back(line + "," + std::to_string(raw));
        else
          rendered.push_back(line);
      }
    }
  }
  sqlite3_close(db);

  // Compare with the source's data lines, less those the repairs removed.
  std::vector<const DataLine *> expected;
  for (size_t i = 0; i < c.data.size(); i++)
    if (!c.dropped.count(i))
      expected.push_back(&c.data[i]);
  if (expected.size() != rendered.size()) {
    report.roundtrip = "rendered " + std::to_string(rendered.size()) + " data lines, expected " +
                       std::to_string(expected.size());
    return false;
  }
  const bool prestag = stored.config.tag_type() == PRESTAG;
  for (size_t i = 0; i < expected.size(); i++) {
    bool exact = false;
    if (prestag && expected[i]->text.find(",V:") != std::string::npos) {
      if (!SameAveragedVoltageLine(expected[i]->text, rendered[i])) {
        report.roundtrip = "line " + std::to_string(expected[i]->number) + ": source \"" +
                           expected[i]->text + "\", rendered mean \"" + rendered[i] + "\"";
        return false;
      }
      report.roundtrip_within_precision++;
      continue;
    }
    if (!SameLine(expected[i]->text, rendered[i], exact)) {
      report.roundtrip = "line " + std::to_string(expected[i]->number) + ": source \"" +
                         expected[i]->text + "\", rendered \"" + rendered[i] + "\"";
      return false;
    }
    (exact ? report.roundtrip_exact : report.roundtrip_within_precision)++;
  }
  report.roundtrip = "passed";
  return true;
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

/**
 * @brief   The latest plausible record time: the read time plus a day.
 *
 * @details A tag whose clock had reset (a flat battery) reports a read time
 *          years in the past, which would make every real record look late;
 *          then the present is the bound instead, and the report says so.
 *
 * @param[in]     h       The parsed header.
 * @param[in,out] report  Notes an implausible read time.
 * @return  Unix seconds.
 */
int64_t LatestPlausible(const ParsedHeader &h, Report &report) {
  const int64_t now = static_cast<int64_t>(std::time(nullptr));
  int64_t bound = now;
  if (h.has_read_time && h.content.read_time_s >= kEarliestEpoch)
    bound = h.content.read_time_s;
  else if (h.has_read_time)
    report.read_time_implausible = true;
  return bound + 86400;
}

/// Variant A state names, from DumpPersistent.c's statestring().
bool VariantAState(const std::string &name, TagState &state) {
  static const std::map<std::string, TagState> names = {
      {"Reset", sRESET},    {"Idle", IDLE},         {"Triggered", CONFIGURED},
      {"Running", RUNNING}, {"Aborted", ABORTED},   {"Finished", FINISHED}};
  const auto it = names.find(name);
  if (it == names.end())
    return false;
  state = it->second;
  return true;
}

/// The last whitespace-separated word of a line, as an integer.
bool LastInt(const std::string &line, int64_t &out) {
  const std::vector<std::string> words = Split(Trim(line), ' ');
  return ParseInt(words.back(), out);
}

/**
 * @brief   Parse a variant A (BitTagRTCv4, DumpPersistent.c) log.
 *
 * @details Header lines may appear anywhere ("# Page N" separates pages).
 *          Data lines are tab-indented `time <local>,<epoch>,<payload>`, the
 *          payload a hex activity word or `temperature T vdd V`. Decoded as
 *          btviz decodes them; see the file comment. Exact duplicate lines
 *          are removed and records outside 2015 to the read time are dropped,
 *          as for the other variants.
 *
 * @param[in]     src    The file.
 * @param[in,out] c      The conversion: header, rows and report.
 * @param[out]    error  Why it does not parse.
 * @return  false on a malformed data line.
 */
bool PrepareVariantA(const SourceFile &src, Conversion &c, std::string &error) {
  Report &report = c.report;
  TextLogHeader &h = c.header.content;
  report.variant = "A";
  h.config.set_tag_type(BITTAG);
  h.info.set_tag_type(BITTAG);
  // Per-minute activity, as the MIN format stores it.
  h.config.set_bittag_log(BITTAG_BITSPERMIN);

  struct Line {
    size_t index;
    int64_t epoch;
    std::string payload;
  };
  std::vector<Line> lines;
  for (size_t i = 0; i < src.lines.size(); i++) {
    const std::string &text = src.lines[i];
    if (Trim(text).empty())
      continue;
    if (text[0] == '#') {
      c.header_text += text + "\n";
      const std::string line = Trim(text.substr(1));
      int64_t n = 0;
      if (line.rfind("Time ", 0) == 0 && line.find("epoch:") != std::string::npos) {
        // "Time <local> epoch: N error E"
        const size_t e = line.find("epoch:");
        const std::vector<std::string> words = Split(Trim(line.substr(e + 6)), ' ');
        double err = 0;
        if (ParseInt(words[0], n)) {
          h.read_time_s = n;
          c.header.has_read_time = true;
        }
        if (words.size() >= 3 && (words[1] == "error" || words[1] == "error:") &&
            ParseDouble(words[2], err))
          h.error_s = err;
      } else if (line.rfind("device:", 0) == 0) {
        c.a_device = Trim(line.substr(7));
      } else if (line.rfind("hash:", 0) == 0) {
        h.info.set_githash(Trim(line.substr(5)));
      } else if (line.rfind("info:", 0) == 0) {
        h.info.set_firmware(Trim(line.substr(5)));
      } else if (line.rfind("flash size =", 0) == 0) {
        double kb = 0;
        if (ParseWithSuffix(line.substr(12), "KB", kb))
          h.info.set_intflashsz(static_cast<int64_t>(kb));
      } else if (line.rfind("State =", 0) == 0) {
        TagState state;
        State entry;
        if (VariantAState(Trim(line.substr(7)), state))
          entry.mutable_status()->set_state(state);
        h.states.push_back(entry);
      } else if (line.rfind("Pages =", 0) == 0 && !h.states.empty()) {
        if (ParseInt(line.substr(7), n))
          h.states.back().mutable_status()->set_internal_data_count(static_cast<int32_t>(n));
      } else if (line.rfind("Time =", 0) == 0 && !h.states.empty()) {
        if (LastInt(line, n))
          h.states.back().mutable_status()->set_millis(n * 1000);
      } else if (line.rfind("start time", 0) == 0) {
        if (LastInt(line, n))
          h.config.mutable_active_interval()->set_start_epoch(static_cast<int32_t>(n));
      } else if (line.rfind("stop time", 0) == 0) {
        if (LastInt(line, n))
          h.config.mutable_active_interval()->set_end_epoch(static_cast<int32_t>(n));
      } else if (line.rfind("Page ", 0) != 0 && line.rfind("pages =", 0) != 0) {
        c.header.unparsed.push_back(line);
      }
      continue;
    }
    if (text[0] != '\t') {
      error = "line " + std::to_string(i + 1) + ": not a variant A data line: " + text;
      return false;
    }
    const std::vector<std::string> parts = Split(Trim(text), ',');
    int64_t epoch = 0;
    if (parts.size() != 3 || parts[0].rfind("time ", 0) != 0 || !ParseInt(parts[1], epoch)) {
      error = "line " + std::to_string(i + 1) + ": malformed: " + text;
      return false;
    }
    lines.push_back({i, epoch, Trim(parts[2])});
  }
  report.unparsed_header = c.header.unparsed;
  report.data_lines = lines.size();
  report.header_only = lines.empty();

  // Variant A's "read time" is the tag's own clock at the dump, which can
  // disagree with its own state log by weeks; only the present bounds it.
  const int64_t latest = static_cast<int64_t>(std::time(nullptr)) + 86400;
  std::set<std::pair<int64_t, std::string>> seen;
  std::map<int64_t, size_t> payloads_at; // epoch+kind -> count, for conflicts
  int64_t last = INT64_MIN;
  for (const Line &line : lines) {
    const std::string &text = src.lines[line.index];
    const bool is_temp = line.payload.rfind("temperature", 0) == 0;
    if (line.epoch < kEarliestEpoch || line.epoch > latest) {
      report.implausible_times++;
      report.dropped_lines.push_back("line " + std::to_string(line.index + 1) + ": " + text);
      continue;
    }
    if (!seen.insert({line.epoch, line.payload}).second) {
      report.exact_duplicates++;
      report.dropped_lines.push_back("line " + std::to_string(line.index + 1) + ": " + text);
      continue;
    }
    if (payloads_at[line.epoch * 2 + (is_temp ? 1 : 0)]++)
      report.conflicting_duplicates++;
    if (!is_temp) {
      if (line.epoch < last)
        report.backward_jumps++;
      last = line.epoch;
    }
    report.records_in++;
    if (is_temp) {
      // "temperature T vdd V"
      const std::vector<std::string> words = Split(line.payload, ' ');
      double t = 0;
      double v = 0;
      if (words.size() != 4 || words[2] != "vdd" || !ParseDouble(words[1], t) ||
          !ParseDouble(words[3], v)) {
        error = "line " + std::to_string(line.index + 1) + ": malformed: " + text;
        return false;
      }
      c.a_core.push_back({line.epoch, t});
      c.a_voltage.push_back({line.epoch, v});
      continue;
    }
    char *end = nullptr;
    const unsigned long word = std::strtoul(line.payload.c_str(), &end, 16);
    if (line.payload.empty() || *end || line.payload.size() > 8) {
      error = "line " + std::to_string(line.index + 1) + ": malformed activity word: " + text;
      return false;
    }
    if (word & 0x40000000UL)
      report.bit30_words++;
    // btviz: for i = 5 .. 1, the minute at epoch - i*60 is bits (i-1)*6.
    for (int i = 5; i >= 1; i--) {
      const unsigned count = static_cast<unsigned>((word >> ((i - 1) * 6)) & 63U);
      if (count > 60)
        report.counts_over_60++;
      c.a_activity.push_back({line.epoch - i * 60, count / 0.6});
    }
    report.records_written++;
  }
  return true;
}

/**
 * @brief   Variant A's check: the written tables hold exactly the rows parsed.
 *
 * @details Variant A has no writer here to render it back with, so the
 *          conversion is a translation; this confirms only that what was
 *          decoded reached the log intact.
 *
 * @param[in]     db_path  The written log.
 * @param[in]     c        The conversion.
 * @param[in,out] report   The result.
 * @return  true when every table's rows match the decoded rows.
 */
bool CheckVariantA(const std::string &db_path, const Conversion &c, Report &report) {
  sqlite3 *db = nullptr;
  std::vector<TagLogSample> act, volt, core;
  const bool read =
      sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK &&
      ReadSeries(db, "SELECT Epoch, Activity FROM Activity ORDER BY rowid", act) &&
      ReadSeries(db, "SELECT Epoch, Voltage FROM Voltage ORDER BY rowid", volt) &&
      ReadSeries(db, "SELECT Epoch, Temperature FROM CoreTemperature ORDER BY rowid", core);
  sqlite3_close(db);
  auto same = [](const std::vector<TagLogSample> &a, const std::vector<TagLogSample> &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); i++)
      if (a[i].epoch != b[i].epoch || a[i].value != b[i].value)
        return false;
    return true;
  };
  if (!read || !same(act, c.a_activity) || !same(volt, c.a_voltage) || !same(core, c.a_core)) {
    report.roundtrip = "variant A rows did not read back as written";
    return false;
  }
  report.roundtrip = "translated: variant A, decoded as btviz decodes it; rows verified, "
                     "no round trip";
  return true;
}

/**
 * @brief   Parse, classify and repair a source file.
 *
 * @param[in]  src     The source.
 * @param[out] c       The conversion.
 * @param[out] error   Why it cannot be converted.
 * @return  0 when convertible, 1 on a malformed file, kExitUnsupported for
 *          a file that is not a log.
 */
int Prepare(const SourceFile &src, Conversion &c, std::string &error) {
  Report &report = c.report;
  report.crlf_lines = src.crlf_lines;
  report.missing_final_newline = src.missing_final_newline;

  size_t first_data = 0;
  std::vector<std::string> header_lines;
  while (first_data < src.lines.size() &&
         (src.lines[first_data].empty() || src.lines[first_data][0] == '#')) {
    if (!src.lines[first_data].empty())
      header_lines.push_back(src.lines[first_data]);
    first_data++;
  }
  for (const std::string &line : header_lines)
    c.header_text += line + "\n";
  if (!ParseHeaderLines(header_lines, c.header, error))
    return 1;
  report.unparsed_header = c.header.unparsed;

  // Variant A: the RTCv4 firmware's tab-indented bitmap lines.
  const bool tabbed = first_data < src.lines.size() && !src.lines[first_data].empty() &&
                      src.lines[first_data][0] == '\t';
  if (c.header.variant_a_marker || tabbed) {
    c = Conversion();
    c.variant_a = true;
    c.report.crlf_lines = src.crlf_lines;
    c.report.missing_final_newline = src.missing_final_newline;
    if (!PrepareVariantA(src, c, error))
      return 1;
    c.report.tag_type = TagType_Name(BITTAG);
    return 0;
  }
  if (!SplitData(src, first_data, c.data, report.data_comments, error))
    return header_lines.empty() ? kExitUnsupported : 1;
  if (header_lines.empty() && c.data.empty()) {
    error = "not a log: no header and no data";
    return kExitUnsupported;
  }
  report.data_lines = c.data.size();

  TextLogHeader &h = c.header.content;
  if (c.header.has_tag_type) {
    report.variant = h.config.tag_type() == PRESTAG ? "D" : "C";
  } else if (c.header.has_board) {
    report.variant = "B";
  } else if (header_lines.empty()) {
    report.variant = "E";
  } else {
    error = "header has neither Tag Type nor Board";
    return kExitUnsupported;
  }
  if (!c.header.has_tag_type) {
    h.config.set_tag_type(BITTAG);
    h.info.set_tag_type(BITTAG);
  }
  if (h.config.tag_type() != BITTAG && h.config.tag_type() != PRESTAG) {
    error = "tag type " + TagType_Name(h.config.tag_type()) + " is not a .txt-era type";
    return kExitUnsupported;
  }
  report.tag_type = TagType_Name(h.config.tag_type());
  report.header_only = c.data.empty();

  const int64_t latest = LatestPlausible(c.header, report);

  if (h.config.tag_type() == PRESTAG) {
    uint32_t period = 0;
    if (!GroupPresTag(c.data, h.config.period(), c.prestag, period, error))
      return 1;
    if (h.config.period() == 0 && !c.prestag.empty()) {
      if (period == 0) {
        error = "PresTag log with no Sample Period and too few samples to infer one";
        return 1;
      }
      h.config.set_period(period);
    }
    report.records_in = c.prestag.size();
    Repair(c.prestag,
           [](const PresTagRecord &a, const PresTagRecord &b) {
             return a.voltage == b.voltage && a.core_temperature == b.core_temperature &&
                    a.pressure == b.pressure && a.temperature == b.temperature;
           },
           latest, c);
    report.records_written = c.prestag.size();
    report.voltage_pairs_averaged = c.prestag.size();
    return 0;
  }

  // BitTag.
  if (!GroupBitTag(c.data, c.bittag, error))
    return 1;
  {
    // Lines GroupBitTag dropped (no closing V:) count as removed.
    std::vector<bool> covered(c.data.size(), false);
    for (const BitTagRecord &r : c.bittag)
      for (size_t index : r.lines)
        covered[index] = true;
    for (size_t i = 0; i < c.data.size(); i++)
      if (!covered[i])
        c.dropped.insert(i);
  }
  report.records_in = c.bittag.size();

  // The format: the header's, else the data's key; they must agree.
  std::string key;
  for (const BitTagRecord &r : c.bittag)
    if (!r.key.empty())
      key = r.key;
  BitTagLogFmt from_data = BITTAG_UNSPECIFIED;
  if (key == "MIN")
    from_data = BITTAG_BITSPERMIN;
  else if (key == "FOURMIN")
    from_data = BITTAG_BITSPERFOURMIN;
  else if (key == "FIVEMIN")
    from_data = BITTAG_BITSPERFIVEMIN;
  if (!c.header.has_bittag_format) {
    if (from_data == BITTAG_UNSPECIFIED && !c.bittag.empty()) {
      error = "no log format in the header and no activity lines to infer it from";
      return 1;
    }
    h.config.set_bittag_log(from_data);
  } else if (from_data != BITTAG_UNSPECIFIED && from_data != h.config.bittag_log()) {
    error = "header log format " + BitTagLogFmt_Name(h.config.bittag_log()) +
            " disagrees with the data's " + key + " lines";
    return 1;
  }
  if (h.config.bittag_log() == BITTAG_UNSPECIFIED) {
    // A header-only file with no format line: nothing to decode.
    return 0;
  }
  if (!GeometryFor(h.config.bittag_log(), c.geometry)) {
    error = "unsupported BitTag log format " + BitTagLogFmt_Name(h.config.bittag_log());
    return 1;
  }

  // The pre-2021 FIVEMIN decode: every record has nine buckets.
  if (c.geometry.format == BITTAG_BITSPERFIVEMIN && !c.bittag.empty()) {
    size_t nine = 0;
    for (const BitTagRecord &r : c.bittag)
      nine += r.pct.size() == 9;
    if (nine == c.bittag.size()) {
      c.buggy = true;
      report.fivemin_9x7 = true;
    } else if (nine) {
      error = "FIVEMIN records mix the 9- and 7-bucket layouts";
      return 1;
    }
  }
  const Geometry &layout = c.buggy ? kBuggyFiveMin : c.geometry;
  for (BitTagRecord &r : c.bittag) {
    uint64_t packed = 0;
    if (!PackRecord(r, layout, packed, error))
      return 1;
    // Nine 7-bit and seven 9-bit buckets cover the same 63 bits, so the
    // buggy layout's word is the stored word.
    r.raw = packed;
    if (r.has_word) {
      c.has_words = true;
      report.words_checked++;
      if (r.word != packed) {
        error = "record ending " + std::to_string(r.epoch) +
                ": packed word does not match its buckets";
        return 1;
      }
    }
  }
  Repair(c.bittag,
         [](const BitTagRecord &a, const BitTagRecord &b) {
           return a.raw == b.raw && a.voltage == b.voltage &&
                  a.core_temperature == b.core_temperature;
         },
         latest, c);
  report.records_written = c.bittag.size();
  return 0;
}

} // namespace

/**
 * @brief   Parse options, convert one file, check it.
 * @return  0 converted and checked (or translated, variant A), 1 failed,
 *          2 not a log.
 */
int main(int argc, char **argv) {
  cxxopts::Options options("tag-txt-import",
                           "Convert a legacy BitTag/PresTag .txt log to a SQLite log, "
                           "verified by rendering it back to text");
  options.add_options()
      ("input", "the .txt log", cxxopts::value<std::string>())
      ("o,output", "SQLite file to write (replaced)", cxxopts::value<std::string>())
      ("root", "record the source path relative to this directory",
       cxxopts::value<std::string>())
      ("report", "also write the conversion report to this JSON file",
       cxxopts::value<std::string>())
      ("keep-failed", "keep the log, marked failed, when the check fails")
      ("d,debug", "debug logging from the SQLite writer")
      ("h,help", "Print usage");
  options.parse_positional({"input"});
  options.positional_help("<log.txt>");
  cxxopts::ParseResult args;
  try {
    args = options.parse(argc, argv);
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n" << options.help() << std::endl;
    return 1;
  }
  if (args.count("help") || !args.count("input") || !args.count("output")) {
    std::cout << options.help() << std::endl;
    return args.count("help") ? 0 : 1;
  }
  const std::string input = args["input"].as<std::string>();
  const std::string output = args["output"].as<std::string>();
  // Error-level logging unless asked, as the other command-line tools do.
  log_set_level(args.count("debug") ? LOG_DEBUG : LOG_ERROR);

  SourceFile src;
  std::string error;
  if (!ReadSource(input, src, error)) {
    std::cerr << "tag-txt-import: " << error << std::endl;
    return 1;
  }
  std::string rel = input;
  if (args.count("root"))
    rel = std::filesystem::relative(input, args["root"].as<std::string>()).generic_string();

  Conversion c;
  const int prepared = Prepare(src, c, error);
  auto write_report = [&]() {
    if (args.count("report")) {
      std::ofstream out(args["report"].as<std::string>());
      out << ReportJson(c.report) << std::endl;
    }
  };
  if (prepared != 0) {
    c.report.roundtrip = "not converted: " + error;
    write_report();
    std::cerr << "tag-txt-import: " << input << ": " << error << std::endl;
    return prepared;
  }

  if (c.report.voltage_pairs_averaged)
    std::cerr << "tag-txt-import: warning: " << input
              << ": PresTag TC: holds a second supply voltage, not a temperature; "
              << c.report.voltage_pairs_averaged
              << " V:/TC: pairs stored as one Voltage, their mean" << std::endl;

  std::unique_ptr<SqliteTagLogWriter> writer;
  if (!WriteLog(c, src, rel, output, writer, error)) {
    writer.reset();
    std::remove(output.c_str());
    c.report.roundtrip = "not written: " + error;
    write_report();
    std::cerr << "tag-txt-import: " << input << ": " << error << std::endl;
    return 1;
  }
  const bool passed =
      c.variant_a ? CheckVariantA(output, c, c.report) : RoundTrip(output, c, c.report);
  const bool recorded = writer->writeInfo("txt_roundtrip", c.report.roundtrip) &&
                        writer->writeInfo("txt_conversion_report", ReportJson(c.report));
  writer.reset();
  write_report();
  if (!passed || !recorded) {
    if (!args.count("keep-failed"))
      std::remove(output.c_str());
    std::cerr << "tag-txt-import: " << input << ": round trip failed: " << c.report.roundtrip
              << std::endl;
    return 1;
  }
  if (c.variant_a) {
    std::cout << input << ": variant A, " << c.report.records_written
              << " activity words translated to " << output
              << " (btviz decode; not round-trip checked)" << std::endl;
    return 0;
  }
  std::cout << input << ": variant " << c.report.variant << ", " << c.report.records_written
            << " records written to " << output << "; round trip passed ("
            << c.report.roundtrip_exact << " lines exact, "
            << c.report.roundtrip_within_precision << " within printed precision)"
            << std::endl;
  return 0;
}
