/**
 * @file    recordlog.cpp
 * @brief   Reading, checking and appending to qtprogram's record CSV.
 */

#include "recordlog.h"

#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace
{

/// Column of the UUID and of the label in a row (header() order).
constexpr int kLabelColumn = 0;
constexpr int kUuidColumn = 2;

/// The columns a file must start with to be taken as a record.
constexpr int kRequiredColumns = 7;

/// Appends @p value to @p list unless it is empty or already there.
void addDistinct(QStringList &list, const QString &value)
{
  if (!value.isEmpty() && !list.contains(value))
    list.append(value);
}

} // namespace

QStringList ProgramRecord::header()
{
  return {"label",        "tag_type", "uuid",  "firmware_version", "git_hash",
          "date",         "test_status", "result", "image_sha256", "image",
          "erase",        "voltage_v", "probe_serial", "host_version"};
}

QStringList ProgramRecord::fields() const
{
  return {label,
          tagType,
          uuid,
          firmwareVersion,
          gitHash,
          date.toUTC().toString(Qt::ISODate),
          testStatus,
          result,
          imageSha256,
          image,
          erase,
          voltage < 0 ? QString() : QString::number(voltage, 'f', 2),
          probeSerial,
          hostVersion};
}

QString RecordLog::quote(const QString &field)
{
  if (!field.contains(',') && !field.contains('"') && !field.contains('\n') &&
      !field.contains('\r'))
    return field;
  QString escaped = field;
  escaped.replace('"', "\"\"");
  return '"' + escaped + '"';
}

QList<QStringList> RecordLog::parse(const QString &text)
{
  QList<QStringList> rows;
  QStringList row;
  QString field;
  bool quoted = false;
  bool any = false;  // the current row has content
  for (qsizetype i = 0; i < text.size(); ++i) {
    const QChar c = text.at(i);
    if (quoted) {
      if (c == '"' && i + 1 < text.size() && text.at(i + 1) == '"') {
        field += '"';
        ++i;
      } else if (c == '"') {
        quoted = false;
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
      any = true;
    } else if (c == ',') {
      row.append(field);
      field.clear();
      any = true;
    } else if (c == '\n' || c == '\r') {
      if (c == '\r' && i + 1 < text.size() && text.at(i + 1) == '\n')
        ++i;
      if (any || !field.isEmpty()) {
        row.append(field);
        rows.append(row);
      }
      row.clear();
      field.clear();
      any = false;
    } else {
      field += c;
      any = true;
    }
  }
  if (any || !field.isEmpty()) {
    row.append(field);
    rows.append(row);
  }
  return rows;
}

bool RecordLog::open(const QString &path, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error)
      *error = QFileInfo::exists(path) ? QObject::tr("Cannot read %1.").arg(path)
                                       : QObject::tr("%1 does not exist.").arg(path);
    return false;
  }
  QList<QStringList> rows = parse(QString::fromUtf8(file.readAll()));
  const QStringList expected = ProgramRecord::header().mid(0, kRequiredColumns);
  if (rows.isEmpty() || rows.first().mid(0, kRequiredColumns) != expected) {
    if (error)
      *error = QObject::tr("%1 is not a Tag Programmer record: its first line should "
                           "be the header \"%2\".")
                   .arg(path, ProgramRecord::header().join(','));
    return false;
  }
  rows.removeFirst();
  rows_ = rows;
  path_ = path;
  return true;
}

bool RecordLog::create(const QString &path, QString *error)
{
  if (QFileInfo::exists(path)) {
    if (error)
      *error = QObject::tr("%1 already exists.").arg(path);
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
      file.write((ProgramRecord::header().join(',') + '\n').toUtf8()) < 0) {
    if (error)
      *error = QObject::tr("Cannot create %1: %2.").arg(path, file.errorString());
    return false;
  }
  file.close();
  path_ = path;
  rows_.clear();
  return true;
}

bool RecordLog::writable(QString *error) const
{
  QFile file(path_);
  if (path_.isEmpty() || !file.open(QIODevice::Append | QIODevice::Text)) {
    if (error)
      *error = path_.isEmpty()
                   ? QObject::tr("No record file is open.")
                   : QObject::tr("Cannot write %1 (%2). Close it in any other program.")
                         .arg(path_, file.errorString());
    return false;
  }
  return true;
}

bool RecordLog::append(const ProgramRecord &record, QString *error)
{
  QFile file(path_);
  if (path_.isEmpty() || !file.open(QIODevice::Append | QIODevice::Text)) {
    if (error)
      *error = QObject::tr("Cannot write %1 (%2).").arg(path_, file.errorString());
    return false;
  }
  QStringList quoted;
  for (const QString &field : record.fields())
    quoted.append(quote(field));
  const QByteArray line = (quoted.join(',') + '\n').toUtf8();
  if (file.write(line) != line.size() || !file.flush()) {
    if (error)
      *error = QObject::tr("Cannot write %1 (%2).").arg(path_, file.errorString());
    return false;
  }
  rows_.append(record.fields());
  return true;
}

QStringList RecordLog::labelsFor(const QString &uuid) const
{
  QStringList labels;
  for (const QStringList &row : rows_)
    if (row.size() > kUuidColumn && row.at(kUuidColumn) == uuid)
      addDistinct(labels, row.at(kLabelColumn));
  return labels;
}

QStringList RecordLog::uuidsFor(const QString &label) const
{
  QStringList uuids;
  for (const QStringList &row : rows_)
    if (row.size() > kUuidColumn && row.at(kLabelColumn) == label)
      addDistinct(uuids, row.at(kUuidColumn));
  return uuids;
}
