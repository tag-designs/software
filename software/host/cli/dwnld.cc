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
  clock_t elapsed = 0;

  cxxopts::Options options("tag-dwnld", "Downloads data from a tag");
  options.add_options()
      ("o,output", "Output file path. Defaults to tag-download.txt or tag-download.db3",
       cxxopts::value<std::string>(output_path))
      ("f,format", "Output format: default, text, sqlite",
       cxxopts::value<std::string>(format_name)->default_value("default"))
      ("s,stop", "Stop a running tag before downloading",
       cxxopts::value<bool>(stop_tag)->default_value("false"));

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

  if ((status.state() == RUNNING) && stop_tag) {
    if (!tag.Stop() || !tag.GetStatus(status)) {
      std::cerr << "Could not stop running tag" << std::endl;
      return 1;
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

  auto writer = createTagLogWriter(storage_format, output_path);
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

  do
  {
    ack.Clear();
    len = 0;

    if (!tag.GetDataLog(ack, total)) {
      finishProgress();
      std::cerr << "Parsing log failed. Unsupported tag type?" << std::endl;
      return 1;
    }

    downloads++;
    if (!ack.error_message().empty()) {
      finishProgress();
      std::cerr << ack.error_message() << std::endl;
      return 1;
    }

    const clock_t start = clock();
    len = writer->writeLog(ack, config);
    elapsed += (clock() - start);

    if (len < 0) {
      finishProgress();
      printWriterError("Could not write log data", *writer);
      return 1;
    }

    total += len;
    printProgress(total, max_count);
  } while (len);

  finishProgress();
  std::cout << "Downloaded " << total << " records in " << downloads << " requests" << std::endl;
  std::cout << "Decoding time " << (static_cast<double>(elapsed) / CLOCKS_PER_SEC)
            << " seconds" << std::endl;
  return 0;
}
