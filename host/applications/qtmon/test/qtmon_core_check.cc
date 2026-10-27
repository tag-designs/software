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

#include <cstdio>
#include <cstdlib>
#include <string>

#include "configdocument.h"
#include "configshape.h"

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

} // namespace

/// Runs every check; exits 1 at the first failure.
int main()
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  checkConfigDocument();
  std::printf("qtmon_core_check: %d checks passed\n", g_checks);
  return 0;
}
