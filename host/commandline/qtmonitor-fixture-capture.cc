/**
 * @file    qtmonitor-fixture-capture.cc
 * @brief   Capture qtmonitor fake-tag fixture data from an attached tag.
 *
 * @details Reads the monitor information, configuration, status, and voltage
 *          responses used by qtmonitor and serializes them as protobuf JSON
 *          inside the documentation fixture schema. The tool is intentionally
 *          read-only: it does not start, stop, erase, test, or otherwise drive
 *          tag state.
 */

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cxxopts.hpp>
#include <google/protobuf/util/json_util.h>
#include <tag.pb.h>
#include <tagclass.h>

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern "C"
{
#include "log.h"
}

namespace
{

template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, int)
    -> decltype(options.always_print_fields_with_no_presence = value, void())
{
  options.always_print_fields_with_no_presence = value;
}

template <typename Options>
auto setAlwaysPrintDefaultFields(Options &options, bool value, long)
    -> decltype(options.always_print_primitive_fields = value, void())
{
  options.always_print_primitive_fields = value;
}

struct CaptureOptions {
  std::string base;
  std::string output = "qtmonitor-fixture.json";
  std::string id;
  std::string label;
  std::string notes;
  std::string fallback_config;
  std::string status_name = "current";
  bool capture_default_config = true;
  bool sanitize = false;
  bool print_summary = false;
  bool debug = false;
};

void intHandler(int dummy)
{
  (void)dummy;
  std::exit(1);
}

/**
 * @brief Convert protobuf enum names into stable lower-kebab fixture ids.
 */
std::string lowerKebab(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    if (ch == '_' || ch == ' ') {
      return '-';
    }
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

/**
 * @brief Escape a string for inclusion in the hand-written JSON envelope.
 */
std::string jsonEscape(const std::string &value)
{
  std::ostringstream out;
  for (const unsigned char ch : value) {
    switch (ch) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (ch < 0x20) {
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(ch) << std::dec << std::setfill(' ');
      } else {
        out << static_cast<char>(ch);
      }
      break;
    }
  }
  return out.str();
}

/**
 * @brief Return the current UTC capture time in ISO-8601 form.
 */
std::string utcTimestamp()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &now_time);
#else
  gmtime_r(&now_time, &tm);
#endif

  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

/**
 * @brief Serialize a protobuf message using the fixture JSON convention.
 *
 * @details Preserves proto field names and prints scalar defaults so default
 *          configuration captures retain fields whose value is zero.
 */
bool messageToJson(const google::protobuf::Message &message,
                   std::string &json,
                   std::string &error)
{
  google::protobuf::util::JsonPrintOptions options;
  options.add_whitespace = true;
  options.preserve_proto_field_names = true;
  setAlwaysPrintDefaultFields(options, true, 0);

  const auto status =
      google::protobuf::util::MessageToJsonString(message, &json, options);
  if (!status.ok()) {
    error = status.ToString();
    return false;
  }
  return true;
}

void addCommonOptions(cxxopts::Options &options)
{
  options.add_options()
      ("b,base", "Select bus:device", cxxopts::value<std::string>())
      ("d,debug", "Set log level to DEBUG")
      ("h,help", "Print usage");
}

/**
 * @brief Parse capture-specific options without mutating the tag.
 */
CaptureOptions parseCaptureOptions(int argc, char **argv)
{
  cxxopts::Options options(
      "qtmonitor-fixture-capture",
      "capture qtmonitor fake-tag fixture data from an attached tag");
  addCommonOptions(options);
  options.add_options()
      ("o,output", "Output fixture JSON path, or '-' for stdout",
       cxxopts::value<std::string>())
      ("id", "Fixture id; defaults to the captured tag type",
       cxxopts::value<std::string>())
      ("label", "Human-readable fixture label; defaults to the tag type",
       cxxopts::value<std::string>())
      ("notes", "Optional fixture notes", cxxopts::value<std::string>())
      ("fallback-config", "Source-tree default config path kept as fallback_ref",
       cxxopts::value<std::string>())
      ("status-name", "Name for the captured status slot",
       cxxopts::value<std::string>())
      ("state", "Alias for --status-name; does not change tag state",
       cxxopts::value<std::string>())
      ("capture-default-config",
       "Capture current GetConfig() as the fixture's default config; this is the default")
      ("no-config", "Do not include the captured Config message")
      ("sanitize", "Replace hardware UUID and local source path placeholders")
      ("print-summary", "Print captured tag summary to stderr");

  CaptureOptions capture;
  try {
    const auto result = options.parse(argc, argv);
    if (result.count("help")) {
      std::cout << options.help() << std::endl;
      std::exit(0);
    }
    if (result.count("base")) {
      capture.base = result["base"].as<std::string>();
    }
    if (result.count("debug")) {
      capture.debug = true;
    }
    if (result.count("output")) {
      capture.output = result["output"].as<std::string>();
    }
    if (result.count("id")) {
      capture.id = result["id"].as<std::string>();
    }
    if (result.count("label")) {
      capture.label = result["label"].as<std::string>();
    }
    if (result.count("notes")) {
      capture.notes = result["notes"].as<std::string>();
    }
    if (result.count("fallback-config")) {
      capture.fallback_config = result["fallback-config"].as<std::string>();
    }
    if (result.count("status-name")) {
      capture.status_name = result["status-name"].as<std::string>();
    }
    if (result.count("state")) {
      capture.status_name = result["state"].as<std::string>();
    }
    if (result.count("no-config")) {
      capture.capture_default_config = false;
    }
    if (result.count("sanitize")) {
      capture.sanitize = true;
    }
    if (result.count("print-summary")) {
      capture.print_summary = true;
    }
  } catch (const cxxopts::OptionException &e) {
    std::cerr << "error parsing options: " << e.what() << std::endl;
    std::cout << options.help() << std::endl;
    std::exit(1);
  }
  return capture;
}

/**
 * @brief Choose the target base, requiring --base when more than one is found.
 */
bool selectDevice(Tag &tag, const CaptureOptions &capture, UsbDev &dev)
{
  std::vector<UsbDev> devs;
  tag.Available(devs);
  if (devs.empty()) {
    std::cerr << "No tag bases found" << std::endl;
    return false;
  }

  if (!capture.base.empty()) {
    unsigned int bus = 0;
    unsigned int address = 0;
    if (sscanf(capture.base.c_str(), "%u:%u", &bus, &address) != 2) {
      std::cerr << "Invalid --base value; expected bus:device" << std::endl;
      return false;
    }

    for (const UsbDev &candidate : devs) {
      if (candidate.bus == bus && candidate.address == address) {
        dev = candidate;
        return true;
      }
    }
    std::cerr << "No matching device " << capture.base << std::endl;
    return false;
  }

  if (devs.size() > 1) {
    std::cerr << "Multiple tag bases found; select one with --base:" << std::endl;
    for (const UsbDev &candidate : devs) {
      std::cerr << "  " << candidate.bus << ":" << candidate.address
                << " 0x" << std::hex << candidate.vid << ":0x"
                << candidate.pid << std::dec << std::endl;
    }
    return false;
  }

  dev = devs.front();
  return true;
}

/**
 * @brief Replace hardware- and workstation-specific TagInfo fields.
 */
void sanitizeInfo(TagInfo &info, const std::string &id)
{
  info.set_uuid("000000000000000000000000");
  if (!id.empty()) {
    info.set_source_path("/embedded/tags/" + id);
  } else {
    info.set_source_path("/embedded/tags/" + lowerKebab(TagType_Name(info.tag_type())));
  }
}

/**
 * @brief Replace state-inapplicable runtime status fields for documentation.
 */
void sanitizeStatus(Status &status)
{
  if (status.state() != sRESET) {
    status.set_sectors_erased(0);
  }
}

/**
 * @brief Compose and write the qtmonitor fake-tag fixture.
 */
bool writeFixture(const CaptureOptions &capture,
                  const TagInfo &info,
                  const Config *config,
                  const Status &status,
                  float voltage)
{
  std::string error;
  std::string info_json;
  std::string config_json;
  std::string status_json;

  if (!messageToJson(info, info_json, error)) {
    std::cerr << "Could not convert TagInfo to JSON: " << error << std::endl;
    return false;
  }
  if (config && !messageToJson(*config, config_json, error)) {
    std::cerr << "Could not convert Config to JSON: " << error << std::endl;
    return false;
  }
  if (!messageToJson(status, status_json, error)) {
    std::cerr << "Could not convert Status to JSON: " << error << std::endl;
    return false;
  }

  const std::string id =
      capture.id.empty() ? lowerKebab(TagType_Name(info.tag_type())) : capture.id;
  const std::string label =
      capture.label.empty() ? TagType_Name(info.tag_type()) : capture.label;

  std::ostringstream out;
  out << "{\n";
  out << "  \"schema\": \"tag-designs.qtmonitor.fake-tag.v1\",\n";
  out << "  \"id\": \"" << jsonEscape(id) << "\",\n";
  out << "  \"label\": \"" << jsonEscape(label) << "\",\n";
  if (!capture.notes.empty()) {
    out << "  \"notes\": \"" << jsonEscape(capture.notes) << "\",\n";
  }
  out << "  \"captured_at_utc\": \"" << utcTimestamp() << "\",\n";
  out << "  \"info\": " << info_json << ",\n";
  if (config) {
    out << "  \"config\": {\n";
    out << "    \"source\": \"captured-default\"";
    if (!capture.fallback_config.empty()) {
      out << ",\n    \"fallback_ref\": \""
          << jsonEscape(capture.fallback_config) << "\"";
    }
    out << ",\n    \"value\": " << config_json << "\n";
    out << "  },\n";
  }
  out << "  \"voltage\": " << std::fixed << std::setprecision(3)
      << static_cast<double>(voltage) << ",\n";
  out << "  \"statuses\": {\n";
  out << "    \"" << jsonEscape(capture.status_name) << "\": " << status_json
      << "\n";
  out << "  }\n";
  out << "}\n";

  if (capture.output == "-") {
    std::cout << out.str();
    return true;
  }

  const std::filesystem::path output_path(capture.output);
  const std::filesystem::path parent = output_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      std::cerr << "Could not create output directory " << parent << ": "
                << ec.message() << std::endl;
      return false;
    }
  }

  std::ofstream file(capture.output, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Could not open " << capture.output << " for writing"
              << std::endl;
    return false;
  }
  file << out.str();
  if (!file.good()) {
    std::cerr << "Could not write " << capture.output << std::endl;
    return false;
  }
  return true;
}

void printSummary(const CaptureOptions &capture,
                  const TagInfo &info,
                  const Status &status,
                  float voltage)
{
  std::cerr << "Captured qtmonitor fixture" << std::endl;
  std::cerr << "  tag_type: " << TagType_Name(info.tag_type()) << std::endl;
  std::cerr << "  firmware: " << info.firmware() << std::endl;
  std::cerr << "  githash: " << info.githash() << std::endl;
  std::cerr << "  state[" << capture.status_name << "]: "
            << TagState_Name(status.state()) << std::endl;
  std::cerr << "  counts: internal=" << status.internal_data_count()
            << " external=" << status.external_data_count() << std::endl;
  std::cerr << "  voltage: " << std::fixed << std::setprecision(3)
            << static_cast<double>(voltage) << std::endl;
  std::cerr << "  output: " << capture.output << std::endl;
}

} // namespace

int main(int argc, char **argv)
{
  CaptureOptions capture = parseCaptureOptions(argc, argv);
  log_set_quiet(true);
  log_set_level(capture.debug ? LOG_DEBUG : LOG_ERROR);

  Tag tag;
  UsbDev dev;
  if (!selectDevice(tag, capture, dev)) {
    return 1;
  }

  if (!tag.Attach(dev)) {
    std::cerr << "Attach failed" << std::endl;
    return 1;
  }

  signal(SIGINT, intHandler);

  TagInfo info;
  Config config;
  Status status;
  float voltage = 0.0f;

  if (!tag.GetTagInfo(info)) {
    std::cerr << "Could not read tag info" << std::endl;
    return 1;
  }
  if (capture.capture_default_config && !tag.GetConfig(config)) {
    std::cerr << "Could not read tag config" << std::endl;
    return 1;
  }
  if (!tag.GetStatus(status)) {
    std::cerr << "Could not read tag status" << std::endl;
    return 1;
  }
  if (!tag.Voltage(voltage)) {
    std::cerr << "Could not read tag voltage" << std::endl;
    return 1;
  }

  if (capture.sanitize) {
    sanitizeInfo(info, capture.id);
  }
  sanitizeStatus(status);

  const Config *captured_config =
      capture.capture_default_config ? &config : nullptr;
  if (!writeFixture(capture, info, captured_config, status, voltage)) {
    return 1;
  }

  if (capture.print_summary) {
    printSummary(capture, info, status, voltage);
  }

  return 0;
}
