/**
 * @file    qtprogram_checks.cpp
 * @brief   Offline assertion checks for qtprogram's station pieces.
 *
 * @details Exercises, with no tag, base or network: the STM32_Programmer_CLI
 *          output parsers against output captured from v2.22.0; the record
 *          CSV's quoting, parsing, appending and duplicate lookups; the
 *          release-archive member filter that keeps extraction inside
 *          `firmware/<Target>/`; the GitHub release-list parser; and, given a
 *          release archive, the whole fetch: download (from a file:// URL),
 *          extraction with the system tar, manifest verification, the cache,
 *          and each image's identity record. See README.md beside this file.
 *
 *          Usage: qtprogram_checks [<tag-firmware-fw-vX.Y.tar.gz>]
 */

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstdlib>

#include "firmwarecatalog.h"
#include "programmercli.h"
#include "recordlog.h"

namespace
{

int failures = 0;

/// Records a failed expectation without stopping, so one run reports them all.
void check(bool ok, const char *what)
{
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

/// `-c port=SWD sn=... mode=UR` on the bench base with a BitTag, 2026-10-24.
const char *kConnectOutput =
    "      -------------------------------------------------------------------\n"
    "                        STM32CubeProgrammer v2.22.0                  \n"
    "      -------------------------------------------------------------------\n"
    "\n"
    "ST-LINK SN  : 0037001A5556431520303537\n"
    "ST-LINK FW  : V2J37S7\n"
    "libusb: warning [darwin_transfer_status] transfer error: timed out\n"
    "Board       : --\n"
    "Voltage     : 3.23V\n"
    "SWD freq    : 4000 KHz\n"
    "Connect mode: Under Reset\n"
    "Reset mode  : Hardware reset\n"
    "Device ID   : 0x435\n"
    "Revision ID : Rev Z\n"
    "Device name : STM32L43xxx/STM32L44xxx\n";

/// `-l st-link` on the same base, colour escapes included as the tool prints them.
const char *kProbeOutput =
    "\x1b[39;49m\x1b[0m                        STM32CubeProgrammer v2.22.0\n"
    "===== STLink Interface =====\n"
    "-------- Connected ST-LINK Probes List --------\n"
    "ST-Link Probe 0 :\n"
    "   ST-LINK SN  : 0037001A5556431520303537\n"
    "   ST-LINK FW  : V2J37S7\n"
    "   Access Port Number  : 0\n"
    "-----------------------------------------------\n";

void checkCliParsers()
{
  check(parseProgrammerVersion(kConnectOutput) == "2.22.0", "version from connect output");
  check(parseProgrammerVersion(kProbeOutput) == "2.22.0", "version through colour escapes");
  check(stripAnsi("\x1b[31mred\x1b[0m") == "red", "ANSI escapes stripped");

  const QList<ProbeInfo> probes = parseProbeList(kProbeOutput);
  check(probes.size() == 1, "one probe listed");
  check(!probes.isEmpty() && probes.first().serial == "0037001A5556431520303537", "probe serial");
  check(!probes.isEmpty() && probes.first().firmware == "V2J37S7", "probe firmware");

  const ConnectInfo ok = parseConnect(kConnectOutput, 0);
  check(ok.ok, "connect succeeded");
  check(ok.deviceId == kDeviceIdL432, "device ID 0x435");
  check(qAbs(ok.voltage - 3.23) < 1e-9, "target voltage 3.23 V");
  check(ok.deviceName == "STM32L43xxx/STM32L44xxx", "device name");

  const ConnectInfo noTarget =
      parseConnect("ST-LINK SN  : 0037\nError: No STM32 target found!\n", 1);
  check(!noTarget.ok && noTarget.error == "No STM32 target found!", "first Error: line kept");
  const ConnectInfo silent = parseConnect("nothing useful\n", 0);
  check(!silent.ok && !silent.error.isEmpty(), "exit 0 without a device ID is a failure");
}

void checkRecord(const QString &dir)
{
  check(RecordLog::quote("plain") == "plain", "plain field unquoted");
  check(RecordLog::quote("a,b") == "\"a,b\"", "comma quoted");
  check(RecordLog::quote("say \"hi\"") == "\"say \"\"hi\"\"\"", "quotes doubled");

  const QList<QStringList> rows = RecordLog::parse("a,\"b,c\",\"d\"\"e\"\r\n\"x\ny\",,z\n");
  check(rows.size() == 2, "two rows parsed");
  check(rows.value(0) == QStringList({"a", "b,c", "d\"e"}), "quoted comma and quote");
  check(rows.value(1) == QStringList({"x\ny", "", "z"}), "quoted line break and empty field");

  const QString path = QDir(dir).filePath("record.csv");
  RecordLog log;
  QString error;
  check(!log.open(path, &error) && !error.isEmpty(), "missing record refused");
  check(log.create(path, &error), "record created");
  check(!log.create(path, &error), "existing record not overwritten");
  check(log.writable(&error), "record writable");

  ProgramRecord row;
  row.label = "BitTag, 017";  // a comma, to exercise quoting end to end
  row.tagType = "BitTag";
  row.uuid = "2035374D303150190059002F";
  row.firmwareVersion = "fw-v0.7";
  row.gitHash = "169cda2c";
  row.date = QDateTime(QDate(2026, 10, 24), QTime(12, 0), QTimeZone::UTC);
  row.result = "OK";
  row.voltage = 3.23;
  check(log.append(row, &error), "row appended");
  row.label = "BitTag-018";
  row.uuid = "OTHER";
  check(log.append(row, &error), "second row appended");

  RecordLog reread;
  check(reread.open(path, &error), "record reopened");
  check(reread.count() == 2, "two rows read back");
  check(reread.labelsFor("2035374D303150190059002F") == QStringList({"BitTag, 017"}),
        "label found by UUID");
  check(reread.uuidsFor("BitTag-018") == QStringList({"OTHER"}), "UUID found by label");

  QFile file(path);
  check(file.open(QIODevice::ReadOnly), "record readable");
  const QList<QByteArray> lines = file.readAll().split('\n');
  check(lines.value(0) == ProgramRecord::header().join(',').toUtf8(), "header line");
  check(lines.value(1).startsWith("\"BitTag, 017\",BitTag,2035374D303150190059002F,fw-v0.7,"
                                  "169cda2c,2026-10-24T12:00:00Z,,OK,"),
        "first seven columns in the specification's order");
  check(lines.value(1).contains(",3.23,"), "voltage written");

  const QString other = QDir(dir).filePath("other.csv");
  QFile foreign(other);
  check(foreign.open(QIODevice::WriteOnly), "foreign CSV written");
  foreign.write("name,value\n");
  foreign.close();
  check(!reread.open(other, &error), "a CSV with another header is refused");
  check(reread.path() == path && reread.count() == 2, "a refused open keeps the open record");
}

void checkMembers()
{
  check(FirmwareCatalog::acceptableMember("firmware/BitTag/BitTag.elf"), "image accepted");
  check(FirmwareCatalog::acceptableMember("firmware/BitTag/BitTag-build-manifest.json"),
        "manifest accepted");
  for (const char *bad : {"firmware/BitTag/BitTag.bin", "firmware/BitTag/Other.elf",
                          "firmware/../BitTag/BitTag.elf", "../firmware/BitTag/BitTag.elf",
                          "/firmware/BitTag/BitTag.elf", "firmware/BitTag/../BitTag.elf",
                          "firmware/Bit Tag/Bit Tag.elf", "firmware/licenses/LICENSE",
                          "firmware/BitTag/sub/BitTag.elf"})
    check(!FirmwareCatalog::acceptableMember(bad), bad);
  check(FirmwareCatalog::isFirmwareTag("fw-v0.7") && FirmwareCatalog::isFirmwareTag("fw-v1.2.3"),
        "firmware tags");
  check(!FirmwareCatalog::isFirmwareTag("v3.3") && !FirmwareCatalog::isFirmwareTag("fw-vX"),
        "host and malformed tags rejected");
}

/// One release object as the GitHub API returns it.
QJsonObject apiRelease(const QString &tag, bool draft, bool withArchive)
{
  QJsonArray assets;
  assets.append(QJsonObject{{"name", "qualification-BitTag.tar.gz"},
                            {"browser_download_url", "https://example.invalid/q.tar.gz"}});
  if (withArchive)
    assets.append(QJsonObject{{"name", "tag-firmware-" + tag + ".tar.gz"},
                              {"browser_download_url", "https://example.invalid/" + tag}});
  return QJsonObject{{"tag_name", tag},        {"draft", draft},
                     {"prerelease", false},     {"published_at", "2026-10-15T11:25:25Z"},
                     {"html_url", "https://example.invalid/page"}, {"assets", assets}};
}

void checkReleaseList()
{
  QJsonArray api;
  api.append(apiRelease("v3.3", false, true));       // host release
  api.append(apiRelease("fw-v0.6", false, false));   // no archive
  api.append(apiRelease("fw-v0.7", false, true));
  api.append(apiRelease("fw-v0.10", false, true));   // orders after 0.7
  api.append(apiRelease("fw-v0.8", true, true));     // draft
  const QList<FirmwareRelease> releases =
      FirmwareCatalog::parseReleases(QJsonDocument(api).toJson());
  check(releases.size() == 2, "only published fw-v releases with an archive");
  check(releases.value(0).tag == "fw-v0.10" && releases.value(1).tag == "fw-v0.7",
        "newest version first");
  check(releases.value(1).archiveUrl.toString() == "https://example.invalid/fw-v0.7",
        "archive URL");
}

/// Waits for @p signal-like completion: runs the loop until @p done is set or 60 s pass.
void waitFor(const bool &done)
{
  QEventLoop loop;
  QTimer poll;
  QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
    if (done)
      loop.quit();
  });
  poll.start(50);
  QTimer::singleShot(60000, &loop, &QEventLoop::quit);
  loop.exec();
}

void checkFetch(const QString &archive, const QString &dir)
{
  const QString tag = QFileInfo(archive).fileName().remove("tag-firmware-").remove(".tar.gz");
  const QString cache = QDir(dir).filePath("cache");
  QDir().mkpath(cache);
  // The listing the catalog reads at start-up, pointing at the local archive.
  QFile listing(QDir(cache).filePath("releases.json"));
  check(listing.open(QIODevice::WriteOnly), "listing written");
  listing.write(QJsonDocument(QJsonArray{QJsonObject{
                                  {"tag", tag},
                                  {"archive", QUrl::fromLocalFile(archive).toString()}}})
                    .toJson());
  listing.close();

  FirmwareCatalog catalog(cache);
  check(!catalog.isCached(tag), "not cached before the fetch");
  bool done = false;
  QString failure;
  QObject::connect(&catalog, &FirmwareCatalog::fetched, [&](const QString &) { done = true; });
  QObject::connect(&catalog, &FirmwareCatalog::fetchFailed,
                   [&](const QString &, const QString &why) {
                     failure = why;
                     done = true;
                   });
  catalog.fetch(tag);
  waitFor(done);
  check(failure.isEmpty(), qPrintable("fetch succeeded: " + failure));
  check(catalog.isCached(tag), "cached after the fetch");
  const QStringList targets = catalog.targets(tag);
  check(targets.contains("BitTag") && !targets.contains("licenses"),
        "targets are the directories with an image");
  check(!QFileInfo::exists(QDir(cache).filePath(tag + "/firmware/BitTag/BitTag.bin")),
        "only images and manifests extracted");
  for (const FirmwareImage &image : catalog.images(tag)) {
    check(image.usable() && image.manifestVerified,
          qPrintable(image.elfPath + " verified: " + image.error));
    check(image.identity.found && image.identity.target == QFileInfo(image.elfPath).completeBaseName(),
          qPrintable(image.elfPath + " identity names its target"));
  }

  // Corrupt a cached image: loading it again must refuse it.
  QFile elf(catalog.imagePath(tag, "BitTag"));
  check(elf.open(QIODevice::Append), "cached image writable");
  elf.write("x");
  elf.close();
  const FirmwareImage tampered = loadFirmwareImage(catalog.imagePath(tag, "BitTag"), tag);
  check(!tampered.usable(), "a changed cached image is refused");
}

} // namespace

/**
 * @brief   Runs every check and reports the failures.
 * @param[in] argc  1, or 2 with a release archive.
 * @param[in] argv  argv[1], when present: a `tag-firmware-fw-vX.Y.tar.gz` for the fetch check.
 * @return  0 when every check passed, 1 otherwise.
 */
int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  checkCliParsers();
  checkRecord(dir.path());
  checkMembers();
  checkReleaseList();
  if (argc > 1)
    checkFetch(QFileInfo(argv[1]).absoluteFilePath(), dir.path());
  else
    std::printf("No release archive given; the fetch was not checked.\n");
  std::printf(failures ? "%d check(s) FAILED\n" : "All checks passed\n", failures);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
