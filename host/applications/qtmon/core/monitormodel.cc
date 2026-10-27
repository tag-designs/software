/**
 * @file    monitormodel.cc
 * @brief   MonitorModel, declared in monitormodel.h.
 */

#include "monitormodel.h"

#include <algorithm>

#include "tagrules.h"

namespace qtmon {

CommandInputs MonitorModel::commandInputs(bool document, TagType document_type) const
{
  CommandInputs in;
  in.attached = attached_;
  in.state = state();
  in.tag_type = info_.tag_type();
  in.document = document;
  in.document_type = document_type;
  in.downloading = downloading_;
  in.test_pending = test_pending_;
  return in;
}

void MonitorModel::attached(const TagInfo &info)
{
  detached();
  attached_ = true;
  info_ = info;
}

void MonitorModel::detached()
{
  *this = MonitorModel();
}

std::vector<std::string> MonitorModel::takeDebugLines(const std::string &text)
{
  // The firmware's text arrives in pieces across statuses; carriage returns
  // are dropped and an unfinished line waits for its newline.
  std::vector<std::string> lines;
  for (char ch : text) {
    if (ch == '\r')
      continue;
    if (ch == '\n') {
      lines.push_back(debug_line_);
      debug_line_.clear();
      continue;
    }
    debug_line_ += ch;
  }
  return lines;
}

std::vector<MonitorEvent> MonitorModel::applyStatus(const Status &status, float voltage,
                                                    std::int64_t host_ms)
{
  std::vector<MonitorEvent> events;
  for (std::string &line : takeDebugLines(status.debug_message()))
    events.push_back({MonitorEvent::Kind::TagLogLine, std::move(line)});

  has_status_ = true;
  status_ = status;
  status_ms_ = host_ms;
  voltage_ = voltage;
  clock_offset_s_ = clockOffsetSeconds(status.millis(), host_ms);

  if (test_pending_) {
    const std::int64_t elapsed = host_ms - test_sent_ms_;
    if (elapsed >= kTestMinimumMs && status.test_status() != TEST_RUNNING) {
      test_pending_ = false;
      MonitorEvent e{MonitorEvent::Kind::TestFinished};
      e.test = status.test_status();
      events.push_back(e);
    } else if (elapsed > kTestTimeoutMs) {
      test_pending_ = false;
      events.push_back({MonitorEvent::Kind::TestTimedOut});
    }
  }

  if (start_pending_) {
    const TagState s = status.state();
    if (s == RUNNING || s == CONFIGURED) {
      start_pending_ = false;
      MonitorEvent e{MonitorEvent::Kind::StartSucceeded, start_message_};
      e.state = s;
      events.push_back(e);
    } else if (host_ms - start_sent_ms_ >= kStartWaitMs) {
      start_pending_ = false;
      MonitorEvent e{MonitorEvent::Kind::StartFailed, start_message_};
      e.state = s;
      events.push_back(e);
    }
  }

  if (erasing_) {
    if (status.state() == sRESET) {
      erase_value_ = status.sectors_erased();
      erase_maximum_ = std::max(
          erase_maximum_, eraseSectorMaximum(status, info_.tag_type(), info_.extflashsz()));
    } else if (status.state() == IDLE) {
      erasing_ = false;
      events.push_back({MonitorEvent::Kind::EraseFinished});
    }
  }
  return events;
}

std::int64_t MonitorModel::silenceMs(std::int64_t now_ms) const
{
  if (!attached_ || !has_status_ || downloading_)
    return 0;
  return std::max<std::int64_t>(0, now_ms - status_ms_);
}

void MonitorModel::testSent(std::int64_t now_ms)
{
  test_pending_ = true;
  test_sent_ms_ = now_ms;
}

void MonitorModel::startSent(std::int64_t now_ms, const std::string &call_message)
{
  start_pending_ = true;
  start_sent_ms_ = now_ms;
  start_message_ = call_message;
}

void MonitorModel::eraseSent(int sector_size)
{
  erasing_ = true;
  erase_value_ = 0;
  erase_maximum_ = has_status_
                       ? eraseSectorMaximum(status_, info_.tag_type(), info_.extflashsz(),
                                            sector_size)
                       : roundedSectorCount(info_.extflashsz(), sector_size);
}

} // namespace qtmon
