/**
 * @file    downloadjob.cc
 * @brief   DownloadJob, declared in downloadjob.h.
 */

#include "downloadjob.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <utility>

namespace qtmon {

namespace {

/// The writer's own error, or @p fallback when it gave none.
std::string writerError(const TagLogWriter *writer, const char *fallback)
{
  return writer && !writer->lastError().empty() ? writer->lastError() : std::string(fallback);
}

} // namespace

DownloadJob::DownloadJob(Tag &tag, TagLogStorageFormat format, std::string path)
    : tag_(tag), format_(format), path_(std::move(path))
{
}

DownloadResult DownloadJob::run(const std::atomic_bool &cancel, const Progress &progress,
                                const Log &log)
{
  DownloadResult result;
  const auto started = std::chrono::steady_clock::now();
  auto fail = [&](std::string error) {
    log(LogLevel::Error, error);
    result.ok = false;
    result.error = std::move(error);
    return result;
  };

  // The external page count where there is one (IMUTag's NAND); older
  // firmware reports only the internal count.
  Status status;
  if (!tag_.GetStatus(status))
    return fail("Could not read tag status: " + tag_.DebugMessage());
  const int max_count = status.external_data_count() > 0 ? status.external_data_count()
                                                         : status.internal_data_count();
  if (max_count <= 0) {
    log(LogLevel::Info, "No log records to download");
    result.ok = true;
    return result;
  }

  Config config;
  if (!tag_.GetConfig(config))
    return fail("Could not read tag config");
  std::unique_ptr<TagLogWriter> writer = createTagLogWriter(format_, path_, config);
  if (!writer || !writer->isOpen())
    return fail(writerError(writer.get(), "Download output is not open"));
  if (!writer->writeHeader(tag_))
    return fail(writerError(writer.get(), "Download header write failed"));
  if (!writer->beginLog())
    return fail(writerError(writer.get(), "Could not begin log download"));

  int count = 0;
  progress(0, max_count);
  std::string error;
  while (!cancel.load()) {
    Ack ack;
    if (!tag_.GetDataLog(ack, count)) {
      error = "GetDataLog failed at entry " + std::to_string(count) + ": " + tag_.DebugMessage();
      break;
    }
    if (!ack.error_message().empty()) {
      error = ack.error_message();
      break;
    }
    if (ack.err() == Ack::NODATA && count < max_count) {
      log(LogLevel::Info, "skipping missing log block " + std::to_string(count));
      ++count;
      progress(count, max_count);
      if (count >= max_count)
        break;
      continue;
    }
    // The records this page held, which is where the next request starts;
    // 0 at the end of the log; negative when the writer failed.
    const int consumed = writer->writeLog(ack);
    log(LogLevel::Debug,
        "retrieved log block " + std::to_string(count) + " len=" + std::to_string(consumed));
    if (consumed == 0) {
      log(LogLevel::Info, "no data");
      break;
    }
    if (consumed < 0) {
      error = writerError(writer.get(), consumed == -1 ? "no matching log type" : "no log message");
      break;
    }
    count += consumed;
    progress(count, max_count);
    if (count >= max_count)
      break;
  }
  result.cancelled = cancel.load() && error.empty();

  // Commit what was written, whatever ended the loop.
  if (!writer->endLog() && error.empty())
    error = writerError(writer.get(), "Could not finish log download");

  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  char elapsed[64];
  std::snprintf(elapsed, sizeof elapsed, "Download Elapsed time: %.2f seconds", seconds);
  log(LogLevel::Info, elapsed);
  log(LogLevel::Info, "Downloaded " + std::to_string(count) + " blocks");
  progress(count, max_count);

  result.records = count;
  if (!error.empty())
    return fail(error);
  result.ok = true;
  return result;
}

} // namespace qtmon
