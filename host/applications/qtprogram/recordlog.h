/**
 * @file    recordlog.h
 * @brief   The CSV in which qtprogram records every programming attempt.
 *
 * @details One row per attempt that reached the programmer, failures included:
 *          a failed program that left no row could not be told apart from one
 *          that never happened (embedded/tools/flash_release.py, record()).
 *          The image's SHA-256 is recorded because a tag cannot report the
 *          hash of its own image; programming time is the only time it is
 *          known. Rows are appended, never rewritten, so a crash loses at most
 *          the row being written.
 */

#ifndef RECORDLOG_H
#define RECORDLOG_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

/**
 * @struct  ProgramRecord
 * @brief   One row of the record.
 */
struct ProgramRecord
{
  QString label;            ///< The board's physical label, typed by the operator.
  QString tagType;          ///< Build target, e.g. "CompassTagAT25".
  QString uuid;             ///< The MCU's unique ID, as TagInfo reports it.
  QString firmwareVersion;  ///< Release tag, e.g. "fw-v0.7", or "custom".
  QString gitHash;          ///< What the tag reports after programming, else the image's.
  QDateTime date;           ///< When the attempt ended; written in UTC.
  QString testStatus;       ///< TestResult name, or empty if the tests did not run.
  QString result;           ///< OK, TEST_FAILED, PROGRAM_FAILED: (reason), NO_ANSWER, MISMATCH, CANCELLED.
  QString imageSha256;      ///< SHA-256 of the ELF programmed.
  QString image;            ///< The ELF's file name.
  QString erase;            ///< none, below-calibration or all.
  double voltage = -1.0;    ///< Target voltage at the connect check, volts; < 0 if unknown.
  QString probeSerial;      ///< The base's ST-LINK serial.
  QString hostVersion;      ///< qtprogram's version.

  /// The column names, in file order: the specification's seven, then the rest.
  static QStringList header();
  /// This row's fields, in header() order.
  QStringList fields() const;
};

/**
 * @class   RecordLog
 * @brief   Reads and appends to the record CSV.
 *
 * @details open() reads the existing rows for the duplicate checks and the
 *          count; append() writes one row and flushes it. Not thread-safe.
 */
class RecordLog
{
public:
  /**
   * @brief Opens an existing record.
   * @param[in]  path   The CSV.
   * @param[out] error  Why it was refused: missing, unreadable, or a header
   *                    that is not this record's.
   * @return  true when the file was read. On false the record open before, if
   *          any, stays open.
   */
  bool open(const QString &path, QString *error);

  /**
   * @brief Creates a new record containing only the header, and opens it.
   * @param[in]  path   The CSV to create; an existing file is refused.
   * @param[out] error  Why it could not be created.
   * @return  true when created. On false the record open before, if any,
   *          stays open.
   */
  bool create(const QString &path, QString *error);

  /// Forgets the open file.
  void close() { path_.clear(); rows_.clear(); }
  /// The open file, or empty.
  QString path() const { return path_; }
  /// Whether a file is open.
  bool isOpen() const { return !path_.isEmpty(); }
  /// Rows in the file, header excluded.
  int count() const { return static_cast<int>(rows_.size()); }

  /**
   * @brief Whether the file can be appended to now.
   * @details Opens it for append and closes it again. On Windows a file open
   *          in Excel is locked and fails here.
   * @param[out] error  Why not.
   */
  bool writable(QString *error) const;

  /**
   * @brief Appends @p record and flushes it.
   * @param[in]  record  The row.
   * @param[out] error   Why it was not written.
   * @return  true when written.
   */
  bool append(const ProgramRecord &record, QString *error);

  /// Distinct labels recorded for @p uuid, oldest first.
  QStringList labelsFor(const QString &uuid) const;
  /// Distinct UUIDs recorded for @p label, oldest first.
  QStringList uuidsFor(const QString &label) const;

  /**
   * @brief Quotes one field as RFC 4180 requires, when it needs quoting.
   * @param[in] field  Raw text.
   * @return  The field, quoted with doubled quotes if it holds a comma, quote
   *          or line break.
   */
  static QString quote(const QString &field);

  /**
   * @brief Splits RFC 4180 CSV text into rows of fields.
   * @param[in] text  The file's contents.
   * @return  Rows; quoted fields may contain commas, quotes and line breaks.
   */
  static QList<QStringList> parse(const QString &text);

private:
  QString path_;               ///< The open file, or empty.
  QList<QStringList> rows_;    ///< Its rows, header excluded.
};

#endif // RECORDLOG_H
