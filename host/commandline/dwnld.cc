#include "dwnld.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <memory>
#include <string>
#include <time.h>

#include <taglogwriter.h>

using namespace google::protobuf;

#ifdef _WIN64
#include <csignal>
#else
#include <signal.h>
#endif

extern bool parse_options(int argc, char **argv, cxxopts::Options &options, Tag &tag, UsbDev &dev);

namespace
{

static void intHandler(int dummy)
{
  (void)dummy;
  exit(1);
}

std::string lowercase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

bool parseStorageFormat(const std::string &name, TagLogStorageFormat &format)
{
  const std::string normalized = lowercase(name);
  if (normalized == "txt" || normalized == "text") {
    format = TagLogStorageFormat::Text;
    return true;
  }
  if (normalized == "sqlite" || normalized == "sql" || normalized == "db" || normalized == "db3") {
    format = TagLogStorageFormat::Sqlite;
    return true;
  }
  return false;
}

const char *storageFormatName(TagLogStorageFormat format)
{
  switch (format) {
  case TagLogStorageFormat::Sqlite:
    return "sqlite";
  case TagLogStorageFormat::Text:
  default:
    return "text";
  }
}

std::string defaultOutputPath(TagLogStorageFormat format)
{
  return "tag-download" + defaultTagLogExtension(format);
}

void printProgress(int current, int total)
{
  constexpr int bar_width = 40;
  const double ratio = total > 0 ? std::min(1.0, static_cast<double>(current) / total) : 1.0;
  const int filled = static_cast<int>(ratio * bar_width);
  const int percent = static_cast<int>(ratio * 100.0);

  std::cout << "\r[";
  for (int i = 0; i < bar_width; ++i) {
    std::cout << (i < filled ? '#' : '-');
  }
  std::cout << "] " << std::setw(3) << percent << "% "
            << current << "/" << total << std::flush;
}

void finishProgress()
{
  std::cout << std::endl;
}

void printWriterError(const std::string &context, const TagLogWriter &writer)
{
  std::cerr << context;
  if (!writer.lastError().empty()) {
    std::cerr << ": " << writer.lastError();
  }
  std::cerr << std::endl;
}

using SteadyClock = std::chrono::steady_clock;

uint64_t elapsedNs(SteadyClock::time_point start)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          SteadyClock::now() - start)
          .count());
}

double secondsFromNs(uint64_t ns)
{
  return static_cast<double>(ns) / 1000000000.0;
}

void printDurationLine(const char *label, uint64_t total_ns, int count)
{
  std::cout << label << ": " << secondsFromNs(total_ns) << " s";
  if (count > 0) {
    std::cout << " total, " << (secondsFromNs(total_ns) / count)
              << " s/request";
  }
  std::cout << std::endl;
}

void printTransferLine(const char *label, uint64_t calls, uint64_t bytes,
                       uint64_t ns)
{
  std::cout << label << ": calls=" << calls
            << " bytes=" << bytes
            << " time=" << secondsFromNs(ns) << " s";
  if (ns > 0) {
    const double kib_per_sec = (static_cast<double>(bytes) / 1024.0) /
                               secondsFromNs(ns);
    std::cout << " rate=" << kib_per_sec << " KiB/s";
  }
  std::cout << std::endl;
}

void printProfile(const LinkAdaptStats &link_stats,
                  const TagMonitorStats &monitor_stats,
                  uint64_t get_log_ns,
                  uint64_t write_log_ns,
                  int requests)
{
  std::cout << "\nDownload profile" << std::endl;
  printDurationLine("GetDataLog wait", get_log_ns, requests);
  printDurationLine("Decode/write log", write_log_ns, requests);

  if (!link_stats.enabled || !monitor_stats.enabled) {
    std::cout << "Host link instrumentation was compiled out" << std::endl;
    return;
  }

  std::cout << "\nMonitor RPC" << std::endl;
  std::cout << "rpc calls=" << monitor_stats.rpc_calls
            << " request_bytes=" << monitor_stats.request_bytes
            << " response_bytes=" << monitor_stats.response_bytes
            << std::endl;
  printDurationLine("rpc total", monitor_stats.rpc_total_ns,
                    static_cast<int>(monitor_stats.rpc_calls));
  printDurationLine("serialize request", monitor_stats.serialize_ns,
                    static_cast<int>(monitor_stats.rpc_calls));
  printDurationLine("write request buffer", monitor_stats.write_request_ns,
                    static_cast<int>(monitor_stats.rpc_calls));
  printDurationLine("monitor call", monitor_stats.monitor_call_ns,
                    static_cast<int>(monitor_stats.rpc_calls));
  printDurationLine("read response buffer", monitor_stats.read_response_ns,
                    static_cast<int>(monitor_stats.rpc_calls));
  printDurationLine("parse response", monitor_stats.parse_ns,
                    static_cast<int>(monitor_stats.rpc_calls));

  std::cout << "\nLink transport" << std::endl;
  printTransferLine("usb out", link_stats.usb_out_calls,
                    link_stats.usb_out_bytes, link_stats.usb_out_ns);
  printTransferLine("usb in", link_stats.usb_in_calls,
                    link_stats.usb_in_bytes, link_stats.usb_in_ns);
  printDurationLine("stlink command", link_stats.cmd_ns,
                    static_cast<int>(link_stats.cmd_calls));
  printTransferLine("read mem32", link_stats.read_mem_calls,
                    link_stats.read_mem_bytes, link_stats.read_mem_ns);
  printTransferLine("write mem32", link_stats.write_mem_calls,
                    link_stats.write_mem_bytes, link_stats.write_mem_ns);
  printDurationLine("read debug32", link_stats.read_debug_ns,
                    static_cast<int>(link_stats.read_debug_calls));
  printDurationLine("write debug32", link_stats.write_debug_ns,
                    static_cast<int>(link_stats.write_debug_calls));
  printDurationLine("rw status", link_stats.rw_status_ns,
                    static_cast<int>(link_stats.rw_status_calls));
}

} // namespace

int main(int argc, char **argv)
{
  Tag tag;
  UsbDev dev;
  Status status;
  Config config;
  std::string output_path;
  std::string format_name = "default";
  bool stop_tag = false;
  bool profile = false;
  uint64_t get_log_ns = 0;
  uint64_t write_log_ns = 0;

  cxxopts::Options options("tag-dwnld", "Downloads data from a tag");
  options.add_options()
      ("o,output", "Output file path. Defaults to tag-download.txt or tag-download.db3",
       cxxopts::value<std::string>(output_path))
      ("f,format", "Output format: default, text, sqlite",
       cxxopts::value<std::string>(format_name)->default_value("default"))
      ("s,stop", "Stop a running tag before downloading",
       cxxopts::value<bool>(stop_tag)->default_value("false"))
      ("profile", "Print download timing and link transport profile",
       cxxopts::value<bool>(profile)->default_value("false"));

  if (!parse_options(argc, argv, options, tag, dev)) {
    return 1;
  }

  if (!tag.Attach(dev)) {
    std::cerr << "Attach failed" << std::endl;
    return 1;
  }

  signal(SIGINT, intHandler);

  if (!tag.GetStatus(status)) {
    std::cerr << "Could not read tag status" << std::endl;
    return 1;
  }

  if (!status.debug_message().empty()){
      std::cerr << status.debug_message();
  }

  if ((status.state() == RUNNING) && stop_tag) {
    if (!tag.Stop() || !tag.GetStatus(status)) {
      std::cerr << "Could not stop running tag" << std::endl;
      return 1;
    }

    if (!status.debug_message().empty()){
      std::cerr << status.debug_message();
    }
  }

  if ((status.state() != FINISHED) && (status.state() != ABORTED)) {
    std::string formatted;
    TextFormat::PrintToString(status, &formatted);
    std::cerr << "Can't dump logs from current state\n";
    std::cerr << formatted << std::endl;
    return 1;
  }

  if (!tag.GetConfig(config)) {
    std::cerr << "Could not read tag config" << std::endl;
    return 1;
  }

  TagLogStorageFormat storage_format = defaultTagLogStorageFormat(config.tag_type());
  if (lowercase(format_name) != "default") {
    if (!parseStorageFormat(format_name, storage_format)) {
      std::cerr << "Unknown output format: " << format_name << std::endl;
      return 1;
    }
    if (!isTagLogStorageFormatSupported(config.tag_type(), storage_format)) {
      std::cerr << "Format " << storageFormatName(storage_format)
                << " is not supported for tag type "
                << TagType_Name(config.tag_type()) << std::endl;
      return 1;
    }
  }

  if (output_path.empty()) {
    output_path = defaultOutputPath(storage_format);
  }

  auto writer = createTagLogWriter(storage_format, output_path, config);
  if (!writer || !writer->isOpen()) {
    if (writer) {
      printWriterError("Could not open output", *writer);
    } else {
      std::cerr << "Could not create output writer" << std::endl;
    }
    return 1;
  }

  std::cout << "Writing " << storageFormatName(storage_format)
            << " log to " << output_path << std::endl;

  if (!writer->writeHeader(tag)) {
    printWriterError("Could not write log header", *writer);
    return 1;
  }

  const int max_count = status.internal_data_count();
  if (max_count <= 0) {
    std::cout << "No log records to download" << std::endl;
    return 0;
  }

  int len = 0;
  int total = 0;
  int downloads = 0;
  Ack ack;
  printProgress(total, max_count);
  tag.ResetLinkStats();
  tag.ResetMonitorStats();

  auto start = SteadyClock::now();
  if (!writer->beginLog()) {
    finishProgress();
    printWriterError("Could not begin log download", *writer);
    return 1;
  }
  write_log_ns += elapsedNs(start);

  do
  {
    ack.Clear();
    len = 0;

    start = SteadyClock::now();
    if (!tag.GetDataLog(ack, total)) {
      finishProgress();
      std::cerr << "Parsing log failed. Unsupported tag type?" << std::endl;
      return 1;
    }
    get_log_ns += elapsedNs(start);

    downloads++;
    if (!ack.error_message().empty()) {
      finishProgress();
      std::cerr << ack.error_message() << std::endl;
      return 1;
    }

    start = SteadyClock::now();
    len = writer->writeLog(ack);
    write_log_ns += elapsedNs(start);

    if (len < 0) {
      finishProgress();
      printWriterError("Could not write log data", *writer);
      return 1;
    }

    total += len;
    printProgress(total, max_count);
  } while (len);

  start = SteadyClock::now();
  if (!writer->endLog()) {
    finishProgress();
    printWriterError("Could not finish log download", *writer);
    return 1;
  }
  write_log_ns += elapsedNs(start);

  finishProgress();
  std::cout << "Downloaded " << total << " records in " << downloads << " requests" << std::endl;
  std::cout << "Decoding time " << secondsFromNs(write_log_ns)
            << " seconds" << std::endl;
  if (profile) {
    printProfile(tag.GetLinkStats(), tag.GetMonitorStats(), get_log_ns,
                 write_log_ns, downloads);
  }
  return 0;
}
