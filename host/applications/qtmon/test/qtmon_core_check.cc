/**
 * @file    qtmon_core_check.cc
 * @brief   Assertion checks for qtmonitor's Qt-free core (host/applications/qtmon/core).
 *
 * @details A standalone program in the tree's usual style: no framework,
 *          each check prints its name, and the program exits 1 on the first
 *          failure. It covers the rules the window's own checks do not reach
 *          without a tag: the configuration document's approval and
 *          unsaved-changes rules and file round trip, and the monitor
 *          model's handling of statuses (self-test and start waits, erase
 *          progress, the tag's debug text). The command and start-check
 *          tables are checked against test/action-states.txt and
 *          test/start-checks.txt by `qtmonitor --check-*` instead.
 *
 *          Built with -DBUILD_QTMON_CHECKS=ON; see test/README.md.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "configdocument.h"
#include "configshape.h"
#include "monitormodel.h"

namespace {

/// Number of checks run so far, printed at the end.
int g_checks = 0;

/**
 * @brief Fails the program with @p what when @p ok is false.
 * @param[in] ok    The condition that must hold.
 * @param[in] what  What was being checked, printed on failure.
 */
void check(bool ok, const std::string &what)
{
  ++g_checks;
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

/// A minimal configuration of @p type.
Config configOf(TagType type)
{
  Config c;
  c.set_tag_type(type);
  return c;
}

/// The configuration document's rules: approval, modified, save, open.
void checkConfigDocument()
{
  using qtmon::ConfigDocument;
  using qtmon::DocumentSource;

  ConfigDocument doc;
  check(!doc.hasDocument() && doc.tagType() == TAG_UNSPECIFIED, "starts with no document");
  doc.setApproved(true);
  check(!doc.approved(), "approval is ignored with no document");
  check(!doc.isModified(nullptr), "no document is never modified");

  const Config a = configOf(UIUCTAG);
  doc.load(a, DocumentSource::Tag, "");
  doc.setBaseline(&a);
  check(doc.hasDocument() && doc.tagType() == UIUCTAG && doc.source() == DocumentSource::Tag,
        "load sets the tag type and source");
  check(!doc.isModified(&a), "the loaded values are not modified");
  check(doc.isModified(nullptr), "values the panels cannot write count as modified");

  doc.setApproved(true);
  check(doc.approved(), "a document can be approved");
  doc.contentChanged();
  check(!doc.approved(), "a change to the values withdraws approval");

  doc.setApproved(true);
  Config edited = a;
  edited.set_start_delay(5);
  check(doc.isModified(&edited), "edited values are modified");
  doc.saved("/tmp/x.json", edited);
  check(doc.approved(), "saving keeps approval: the values did not change");
  check(doc.source() == DocumentSource::File && doc.path() == "/tmp/x.json",
        "saving makes a file document");
  check(!doc.isModified(&edited), "saved values are the new baseline");

  doc.load(a, DocumentSource::Defaults, "");
  check(!doc.approved(), "loading withdraws approval");
  doc.setBaseline(nullptr);
  check(doc.isModified(&a), "with no baseline every state is modified");

  doc.clear();
  check(!doc.hasDocument() && !doc.approved() && doc.path().empty(), "clear unloads everything");

  // Files: a saved file reads back as written, onto its type's defaults.
  const std::optional<Config> defaults = ConfigShape::defaultConfig(UIUCTAG);
  check(defaults.has_value(), "UIUCTAG has a default configuration");
  const std::optional<std::string> text = ConfigDocument::fileText(*defaults);
  check(text.has_value() && text->find("\"tag_type\": \"UIUCTAG\"") != std::string::npos,
        "the file text names the tag type under its proto field name");
  const qtmon::ParsedFile back = ConfigDocument::parseFile(*text, TAG_UNSPECIFIED);
  check(back.result == qtmon::ParsedFile::Result::Ok &&
            back.config.SerializeAsString() == defaults->SerializeAsString(),
        "a saved file reads back as written");

  check(ConfigDocument::parseFile("{not json", TAG_UNSPECIFIED).result ==
            qtmon::ParsedFile::Result::ParseError,
        "malformed JSON is a parse error");
  check(ConfigDocument::namesNoTagType("{}"), "an empty object names no tag type");
  check(!ConfigDocument::namesNoTagType("{\"tag_type\":\"IMUTAG\"}"),
        "a file with tag_type names one");
  check(!ConfigDocument::namesNoTagType("{not json"), "malformed JSON is not asked about");
  const qtmon::ParsedFile fallback = ConfigDocument::parseFile("{}", IMUTAG);
  check(fallback.result == qtmon::ParsedFile::Result::Ok &&
            fallback.config.tag_type() == IMUTAG,
        "a file naming no type opens as the fallback type's defaults");
  check(ConfigDocument::parseFile("{}", TAG_UNSPECIFIED).result ==
            qtmon::ParsedFile::Result::NoDefaults,
        "no type and no fallback has no defaults");
}


/// A status in @p state with the tag's clock at @p millis.
Status statusOf(TagState state, std::int64_t millis, TestResult test = ALL_PASSED)
{
  Status s;
  s.set_state(state);
  s.set_millis(millis);
  s.set_test_status(test);
  return s;
}

/// The kinds of @p events, in order, as text, for messages.
std::string kinds(const std::vector<qtmon::MonitorEvent> &events)
{
  std::string out;
  for (const qtmon::MonitorEvent &e : events)
    out += std::to_string(static_cast<int>(e.kind)) + " ";
  return out;
}

/// The monitor model's rules: statuses, waits, erase progress, debug lines.
void checkMonitorModel()
{
  using qtmon::MonitorEvent;
  using qtmon::MonitorModel;
  const std::int64_t t0 = 1'800'000'000'000;  // host ms

  MonitorModel m;
  check(!m.attached() && m.state() == STATE_UNSPECIFIED, "starts detached with no state");
  TagInfo info;
  info.set_tag_type(IMUTAG);
  info.set_uuid("U1");
  info.set_extflashsz(1 << 20);
  m.attached(info);
  check(m.attached() && !m.hasStatus(), "attached, no status yet");

  // Clock offset: tag 3 s behind.
  m.applyStatus(statusOf(IDLE, t0 - 3000), 3.7f, t0);
  check(m.clockOffsetS() == -3.0 && m.voltage() == 3.7f && m.state() == IDLE,
        "a status gives the clock offset, voltage and state");

  // Debug text arrives in pieces; lines come out whole, without CR.
  Status piece = statusOf(IDLE, t0);
  piece.set_debug_message("first li");
  check(m.applyStatus(piece, 0, t0).empty(), "an unfinished line waits");
  piece.set_debug_message("ne\r\nsecond\n");
  std::vector<MonitorEvent> lines = m.applyStatus(piece, 0, t0);
  check(lines.size() == 2 && lines[0].text == "first line" && lines[1].text == "second",
        "debug text comes out as whole lines");

  // Self-test: a result is accepted only from 1 s after sending.
  m.testSent(t0);
  check(m.testPending(), "Self-Test awaits its result");
  check(m.applyStatus(statusOf(IDLE, t0, ALL_PASSED), 0, t0 + 400).empty() && m.testPending(),
        "a result before the minimum is taken for the previous run's");
  std::vector<MonitorEvent> e = m.applyStatus(statusOf(IDLE, t0, RTC_FAILED), 0, t0 + 1000);
  check(e.size() == 1 && e[0].kind == MonitorEvent::Kind::TestFinished &&
            e[0].test == RTC_FAILED && !m.testPending(),
        "the result after the minimum finishes the self-test");
  m.testSent(t0);
  check(m.applyStatus(statusOf(TEST, t0, TEST_RUNNING), 0, t0 + 5000).empty(),
        "TEST_RUNNING keeps waiting");
  e = m.applyStatus(statusOf(TEST, t0, TEST_RUNNING), 0, t0 + 60001);
  check(e.size() == 1 && e[0].kind == MonitorEvent::Kind::TestTimedOut && !m.testPending(),
        "no result within 60 s times out");

  // Start: judged by the state reached, not by the call.
  m.startSent(t0, "LIBUSB timeout");
  check(m.applyStatus(statusOf(IDLE, t0), 0, t0 + 1000).empty() && m.startPending(),
        "still IDLE inside the wait is not yet a failure");
  e = m.applyStatus(statusOf(RUNNING, t0), 0, t0 + 2000);
  check(e.size() == 1 && e[0].kind == MonitorEvent::Kind::StartSucceeded &&
            e[0].state == RUNNING && e[0].text == "LIBUSB timeout",
        "reaching RUNNING succeeds, keeping the error the call reported");
  m.startSent(t0, "");
  e = m.applyStatus(statusOf(IDLE, t0), 0, t0 + 7000);
  check(e.size() == 1 && e[0].kind == MonitorEvent::Kind::StartFailed && e[0].state == IDLE,
        "still IDLE after 7 s fails, with the last state: " + kinds(e));
  m.startSent(t0, "");
  e = m.applyStatus(statusOf(CONFIGURED, t0), 0, t0 + 100);
  check(e.size() == 1 && e[0].kind == MonitorEvent::Kind::StartSucceeded,
        "CONFIGURED (waiting for its start time) is success");

  // Erase: progress from sRESET statuses until IDLE; the maximum never falls.
  Status finished = statusOf(FINISHED, t0);
  finished.set_external_data_count(43);  // IMUTag pages
  m.applyStatus(finished, 0, t0);
  m.eraseSent();
  check(m.erasing() && m.eraseMaximum() == 22, "an IMUTag's erase is sized from its pages");
  Status erasing = statusOf(sRESET, t0);
  erasing.set_sectors_erased(5);
  erasing.set_erase_sectors_total_plus_one(31);
  check(m.applyStatus(erasing, 0, t0).empty() && m.eraseValue() == 5 && m.eraseMaximum() == 30,
        "the tag's own total replaces the estimate");
  erasing.set_erase_sectors_total_plus_one(11);
  m.applyStatus(erasing, 0, t0);
  check(m.eraseMaximum() == 30, "the erase maximum never decreases");
  e = m.applyStatus(statusOf(IDLE, t0), 0, t0);
  check(e.size() == 1 && e[0].kind == MonitorEvent::Kind::EraseFinished && !m.erasing(),
        "IDLE ends the erase");

  // Silence: the time since the latest status, except while downloading.
  m.applyStatus(statusOf(IDLE, t0), 0, t0);
  check(m.silenceMs(t0 + 500) == 500, "silence counts from the latest status");
  check(m.silenceMs(t0 + qtmon::kNoAnswerMs) >= qtmon::kNoAnswerMs,
        "no status for kNoAnswerMs is no answer");

  // The command rule's inputs.
  m.downloadStarted();
  check(m.silenceMs(t0 + 60000) == 0, "a download pauses the polls on purpose: not silence");
  m.testSent(t0);
  const qtmon::CommandInputs in = m.commandInputs(true, IMUTAG);
  check(in.attached && in.state == IDLE && in.tag_type == IMUTAG && in.document &&
            in.document_type == IMUTAG && in.downloading && in.test_pending,
        "commandInputs carries the model and the document");

  // Detach forgets the tag and every wait.
  m.detached();
  check(!m.attached() && !m.hasStatus() && !m.testPending() && !m.downloading() &&
            m.info().uuid().empty(),
        "detach forgets everything");
  check(m.silenceMs(t0 + 60000) == 0, "a detached tag is not silent");
}

} // namespace

/// Runs every check; exits 1 at the first failure.
int main()
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  checkConfigDocument();
  checkMonitorModel();
  std::printf("qtmon_core_check: %d checks passed\n", g_checks);
  return 0;
}
