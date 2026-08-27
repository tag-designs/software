/**
 * @file    dataprocessing.cc
 * @brief   Materialize derived sensor streams into copied SQLite logs.
 *
 * @details The first DataProcessing implementation copies an input SQLite log
 *          to a separate output database, adds processing provenance, and can
 *          materialize CompassTag calibrated vectors and canonical orientation
 *          streams. Display-only heading conventions such as declination and
 *          battery-forward direction are deliberately not stored; downstream
 *          tools can derive those from the materialized yaw stream.
 */

#include "compass_processor.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

#include <sqlite3.h>

#include <iostream>

namespace
{

constexpr int kProcessorVersion = 1;

enum class IfExists
{
    Fail,
    Replace,
    Keep,
};

/**
 * @struct  ToolOptions
 * @brief   Parsed command-line options for one DataProcessing run.
 */
struct ToolOptions
{
    QString inputPath;
    QString outputPath;
    QStringList processors;
    IfExists ifExists = IfExists::Fail;
    bool dryRun = false;
    bool listProcessors = false;
    QString describeProcessor;
    bool printSummary = false;
};

/**
 * @struct  CompassSample
 * @brief   One raw CompassTag row from the input SQLite log.
 */
struct CompassSample
{
    qint64 epoch = 0;
    QVector3D accel;
    QVector3D mag;
};

/**
 * @struct  CompassInput
 * @brief   Loaded CompassTag samples and derived values for processors.
 */
struct CompassInput
{
    qint64 calibrationEpoch = 0;
    CompassCalibration calibration;
    QVector<CompassSample> rawSamples;
    QVector<QVector3D> calibratedMag;
    QVector<CompassDerivedSample> derived;
};

/**
 * @struct  StreamDefinition
 * @brief   Metadata row inserted into the SQLite streams catalog.
 */
struct StreamDefinition
{
    const char *id;
    const char *groupId;
    const char *groupName;
    const char *table;
    const char *timeColumn;
    const char *valueColumn;
    const char *kind;
    const char *displayName;
    const char *units;
    const char *quantity;
    const char *comment;
};

/**
 * @class   Database
 * @brief   Small RAII wrapper around one sqlite3 handle.
 */
class Database
{
public:
    Database(const QString &path, int flags)
    {
        const QByteArray utf8Path = path.toUtf8();
        if (sqlite3_open_v2(utf8Path.constData(), &db_, flags, nullptr) != SQLITE_OK) {
            lastError_ = errorString();
            close();
        }
    }

    ~Database()
    {
        close();
    }

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool isOpen() const
    {
        return db_ != nullptr;
    }

    sqlite3 *get() const
    {
        return db_;
    }

    QString lastError() const
    {
        return lastError_.isEmpty() ? errorString() : lastError_;
    }

    bool exec(const char *sql, QString &error)
    {
        char *sqliteError = nullptr;
        if (sqlite3_exec(db_, sql, nullptr, nullptr, &sqliteError) != SQLITE_OK) {
            error = QString::fromUtf8(sqliteError ? sqliteError : sqlite3_errmsg(db_));
            sqlite3_free(sqliteError);
            return false;
        }
        return true;
    }

private:
    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    void close()
    {
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

    sqlite3 *db_ = nullptr;
    QString lastError_;
};

/**
 * @class   Statement
 * @brief   RAII wrapper around one prepared sqlite3 statement.
 */
class Statement
{
public:
    Statement(Database &db, const char *sql) : db_(db.get())
    {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            lastError_ = errorString();
            stmt_ = nullptr;
        }
    }

    ~Statement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    bool valid() const
    {
        return stmt_ != nullptr;
    }

    QString lastError() const
    {
        return lastError_;
    }

    bool bindInt64(int index, qint64 value)
    {
        return check(sqlite3_bind_int64(stmt_, index, value));
    }

    bool bindDouble(int index, double value)
    {
        return check(sqlite3_bind_double(stmt_, index, value));
    }

    bool bindText(int index, const QString &value)
    {
        const QByteArray utf8 = value.toUtf8();
        return check(sqlite3_bind_text(
            stmt_,
            index,
            utf8.constData(),
            utf8.size(),
            SQLITE_TRANSIENT));
    }

    bool bindNullableText(int index, const char *value)
    {
        if (!value) {
            return check(sqlite3_bind_null(stmt_, index));
        }
        return bindText(index, QString::fromUtf8(value));
    }

    int step()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            lastError_ = errorString();
        }
        return rc;
    }

    bool stepDone()
    {
        const int rc = step();
        if (rc != SQLITE_DONE) {
            if (lastError_.isEmpty()) {
                lastError_ = QStringLiteral("statement returned rows unexpectedly");
            }
            return false;
        }
        sqlite3_reset(stmt_);
        sqlite3_clear_bindings(stmt_);
        return true;
    }

    qint64 int64Column(int index) const
    {
        return sqlite3_column_int64(stmt_, index);
    }

    double doubleColumn(int index) const
    {
        return sqlite3_column_double(stmt_, index);
    }

    QString textColumn(int index) const
    {
        const unsigned char *text = sqlite3_column_text(stmt_, index);
        return QString::fromUtf8(text ? reinterpret_cast<const char *>(text) : "");
    }

private:
    bool check(int rc)
    {
        if (rc != SQLITE_OK) {
            lastError_ = errorString();
            return false;
        }
        return true;
    }

    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    QString lastError_;
};

/**
 * @brief   Serializes a JSON object for storage in provenance metadata.
 *
 * @param[in] object   Object to encode without extra whitespace.
 *
 * @return  Compact UTF-8 JSON as a QString.
 */
QString jsonCompact(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/**
 * @brief   Serializes a JSON array for storage in provenance metadata.
 *
 * @param[in] array   Array to encode without extra whitespace.
 *
 * @return  Compact UTF-8 JSON as a QString.
 */
QString jsonCompact(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

/**
 * @brief   Calculates a SHA-256 hash for the source SQLite file.
 *
 * @param[in] path    File to hash.
 * @param[out] error  Populated with an explanatory message on failure.
 *
 * @return  Lowercase hexadecimal SHA-256 digest, or an empty string if the
 *          file could not be opened or read.
 */
QString sha256File(const QString &path, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Could not open %1: %2").arg(path, file.errorString());
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        hash.addData(file.read(1024 * 1024));
    }
    return QString::fromLatin1(hash.result().toHex());
}

/**
 * @brief   Checks whether a SQLite table exists.
 *
 * @param[in,out] db   Open database handle.
 * @param[in] name     Table name to find in @c sqlite_master.
 * @param[out] exists  Set to true when the table exists.
 * @param[out] error   Populated with the SQLite error text on failure.
 *
 * @return  true when the catalog query completed, false on SQLite failure.
 */
bool tableExists(Database &db, const QString &name, bool &exists, QString &error)
{
    Statement stmt(
        db,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=? LIMIT 1");
    if (!stmt.valid() || !stmt.bindText(1, name)) {
        error = stmt.lastError();
        return false;
    }
    const int rc = stmt.step();
    if (rc == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (rc == SQLITE_DONE) {
        exists = false;
        return true;
    }
    error = stmt.lastError();
    return false;
}

/**
 * @brief   Checks whether a stream metadata row exists.
 *
 * @param[in,out] db   Open database handle.
 * @param[in] id       Stream id to search for in @c streams.
 * @param[out] exists  Set to true when the stream id exists.
 * @param[out] error   Populated with the SQLite error text on failure.
 *
 * @return  true when the query completed, false on SQLite failure.
 */
bool streamExists(Database &db, const QString &id, bool &exists, QString &error)
{
    Statement stmt(db, "SELECT 1 FROM streams WHERE stream_id=? LIMIT 1");
    if (!stmt.valid() || !stmt.bindText(1, id)) {
        error = stmt.lastError();
        return false;
    }
    const int rc = stmt.step();
    if (rc == SQLITE_ROW) {
        exists = true;
        return true;
    }
    if (rc == SQLITE_DONE) {
        exists = false;
        return true;
    }
    error = stmt.lastError();
    return false;
}

/**
 * @brief   Counts rows in a known SQLite table.
 *
 * @param[in,out] db  Open database handle.
 * @param[in] table   Trusted table name selected by the caller.
 * @param[out] count  Number of rows reported by SQLite.
 * @param[out] error  Populated with the SQLite error text on failure.
 *
 * @return  true when the count query completed, false on SQLite failure.
 *
 * @warning @p table is interpolated into SQL. Pass only fixed internal table
 *          names, not user-provided strings.
 */
bool rowCount(Database &db, const char *table, qint64 &count, QString &error)
{
    const QString sql = QStringLiteral("SELECT COUNT(*) FROM %1").arg(table);
    const QByteArray utf8 = sql.toUtf8();
    Statement stmt(db, utf8.constData());
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }
    const int rc = stmt.step();
    if (rc != SQLITE_ROW) {
        error = stmt.lastError();
        return false;
    }
    count = stmt.int64Column(0);
    return true;
}

QStringList compassProcessorIds()
{
    return {QStringLiteral("compass-calibrated"), QStringLiteral("compass-orientation")};
}

/**
 * @brief   Returns the user-facing description for a processor id.
 *
 * @param[in] processor   Processor id from the command line.
 *
 * @return  Description text, or an empty string if the id is unknown.
 */
QString processorDescription(const QString &processor)
{
    if (processor == QStringLiteral("compass-calibrated")) {
        return QStringLiteral(
            "Writes CompassCalibrated with raw acceleration and calibrated "
            "magnetometer x/y/z columns, plus record-column stream metadata.");
    }
    if (processor == QStringLiteral("compass-orientation")) {
        return QStringLiteral(
            "Writes CompassOrientation with canonical magnetic-frame yaw, pitch, "
            "roll, dip, field, acceleration magnitude, and quaternion columns.");
    }
    return {};
}

/**
 * @brief   Tests whether a database has the inputs required by Compass processors.
 *
 * @param[in,out] db  Open SQLite log database.
 * @param[out] reason Success details or the first missing requirement.
 *
 * @return  true when raw compass samples and calibration rows are available.
 */
bool compassInputsAvailable(Database &db, QString &reason)
{
    QString error;
    bool hasCompass = false;
    bool hasCalibration = false;
    if (!tableExists(db, QStringLiteral("Compass"), hasCompass, error)) {
        reason = error;
        return false;
    }
    if (!hasCompass) {
        reason = QStringLiteral("missing Compass table");
        return false;
    }
    if (!tableExists(db, QStringLiteral("Calibration"), hasCalibration, error)) {
        reason = error;
        return false;
    }
    if (!hasCalibration) {
        reason = QStringLiteral("missing Calibration table");
        return false;
    }

    qint64 compassRows = 0;
    qint64 calibrationRows = 0;
    if (!rowCount(db, "Compass", compassRows, error)
        || !rowCount(db, "Calibration", calibrationRows, error)) {
        reason = error;
        return false;
    }
    if (compassRows <= 0) {
        reason = QStringLiteral("Compass table is empty");
        return false;
    }
    if (calibrationRows <= 0) {
        reason = QStringLiteral("Calibration table is empty");
        return false;
    }

    reason = QStringLiteral("%1 Compass rows, %2 calibration rows")
                 .arg(compassRows)
                 .arg(calibrationRows);
    return true;
}

/**
 * @brief   Loads raw compass rows, latest calibration constants, and derived samples.
 *
 * @param[in,out] db   Open SQLite log database.
 * @param[out] input   Populated CompassTag samples and derived values.
 * @param[out] error   Populated with the reason loading or derivation failed.
 *
 * @return  true when all CompassTag processor inputs were loaded.
 */
bool loadCompassInput(Database &db, CompassInput &input, QString &error)
{
    QString reason;
    if (!compassInputsAvailable(db, reason)) {
        error = reason;
        return false;
    }

    Statement calibration(
        db,
        "SELECT Epoch, Constants FROM Calibration ORDER BY Epoch DESC LIMIT 1");
    if (!calibration.valid()) {
        error = calibration.lastError();
        return false;
    }
    if (calibration.step() != SQLITE_ROW) {
        error = QStringLiteral("Calibration table is empty");
        return false;
    }

    input.calibrationEpoch = calibration.int64Column(0);
    const QString constantsJson = calibration.textColumn(1);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(constantsJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("Latest calibration constants are not valid JSON: %1")
                    .arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonValue magnetometer = root.value(QStringLiteral("magnetometer"));
    if (!magnetometer.isObject()) {
        error = QStringLiteral("Latest calibration constants do not contain a magnetometer object");
        return false;
    }
    input.calibration = CompassCalibration::fromMagnetometerJson(magnetometer.toObject());

    Statement rows(db, "SELECT Epoch, ax, ay, az, mx, my, mz FROM Compass ORDER BY Epoch");
    if (!rows.valid()) {
        error = rows.lastError();
        return false;
    }

    CompassProcessor processor(input.calibration);
    input.rawSamples.clear();
    input.calibratedMag.clear();
    input.derived.clear();
    while (true) {
        const int rc = rows.step();
        if (rc == SQLITE_DONE) {
            break;
        }
        if (rc != SQLITE_ROW) {
            error = rows.lastError();
            return false;
        }

        CompassSample sample;
        sample.epoch = rows.int64Column(0);
        sample.accel = QVector3D(
            rows.doubleColumn(1),
            rows.doubleColumn(2),
            rows.doubleColumn(3));
        sample.mag = QVector3D(
            rows.doubleColumn(4),
            rows.doubleColumn(5),
            rows.doubleColumn(6));

        CompassRawSample raw;
        raw.epoch = sample.epoch;
        raw.accel = sample.accel;
        raw.mag = sample.mag;

        CompassDerivedSample derived;
        if (!processor.deriveSample(raw, derived)) {
            error = QStringLiteral("Could not derive orientation at epoch %1").arg(sample.epoch);
            return false;
        }

        input.rawSamples.append(sample);
        input.calibratedMag.append(input.calibration.apply(sample.mag));
        input.derived.append(derived);
    }

    if (input.rawSamples.isEmpty()) {
        error = QStringLiteral("Compass table is empty");
        return false;
    }
    return true;
}

/**
 * @brief   Removes existing stream metadata rows for a processor output.
 *
 * @param[in,out] db      Open processed database.
 * @param[in] streamIds   Stream ids owned by the selected processor.
 * @param[out] error      Populated with SQLite error text on failure.
 *
 * @return  true when every requested row was deleted or did not exist.
 */
bool deleteStreamIds(Database &db, const QStringList &streamIds, QString &error)
{
    Statement stmt(db, "DELETE FROM streams WHERE stream_id=?");
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }
    for (const QString &id : streamIds) {
        if (!stmt.bindText(1, id) || !stmt.stepDone()) {
            error = stmt.lastError();
            return false;
        }
    }
    return true;
}

/**
 * @brief   Inserts stream metadata rows for newly materialized columns.
 *
 * @param[in,out] db   Open processed database.
 * @param[in] streams  Stream definitions owned by one processor.
 * @param[out] error   Populated with SQLite error text on failure.
 *
 * @return  true when all stream rows were inserted.
 */
bool insertStreamDefinitions(
    Database &db,
    const QVector<StreamDefinition> &streams,
    QString &error)
{
    Statement stmt(
        db,
        "INSERT INTO streams ("
        "stream_id, group_id, group_name, table_name, time_column, value_column, "
        "stream_kind, display_name, units, quantity, comment"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }

    for (const StreamDefinition &stream : streams) {
        if (!stmt.bindText(1, QString::fromUtf8(stream.id))
            || !stmt.bindNullableText(2, stream.groupId)
            || !stmt.bindNullableText(3, stream.groupName)
            || !stmt.bindText(4, QString::fromUtf8(stream.table))
            || !stmt.bindText(5, QString::fromUtf8(stream.timeColumn))
            || !stmt.bindText(6, QString::fromUtf8(stream.valueColumn))
            || !stmt.bindText(7, QString::fromUtf8(stream.kind))
            || !stmt.bindText(8, QString::fromUtf8(stream.displayName))
            || !stmt.bindNullableText(9, stream.units)
            || !stmt.bindNullableText(10, stream.quantity)
            || !stmt.bindNullableText(11, stream.comment)
            || !stmt.stepDone()) {
            error = stmt.lastError();
            return false;
        }
    }
    return true;
}

/**
 * @brief   Ensures the provenance table exists in the processed database.
 *
 * @param[in,out] db  Open processed database.
 * @param[out] error  Populated with SQLite error text on failure.
 *
 * @return  true when the table exists or was created.
 */
bool createProcessingRunTable(Database &db, QString &error)
{
    return db.exec(
        "CREATE TABLE IF NOT EXISTS ProcessingRun ("
        "RunId INTEGER PRIMARY KEY,"
        "ToolName TEXT,"
        "ToolVersion TEXT,"
        "ProcessorId TEXT,"
        "ProcessorVersion INTEGER,"
        "CreatedUtc TEXT,"
        "InputFileName TEXT,"
        "InputSha256 TEXT,"
        "ConfigurationJson TEXT,"
        "SourceTablesJson TEXT,"
        "OutputTablesJson TEXT,"
        "Status TEXT"
        ");",
        error);
}

/**
 * @brief   Records one successful processor invocation.
 *
 * @param[in,out] db       Open processed database.
 * @param[in] options      Parsed command-line options for the run.
 * @param[in] inputHash    SHA-256 digest of the original input file.
 * @param[in] processor    Processor id that produced the outputs.
 * @param[in] calibrationEpoch Epoch of the calibration row used, in log units.
 * @param[in] sourceTables Tables read by the processor.
 * @param[in] outputTables Tables written by the processor.
 * @param[out] error       Populated with SQLite error text on failure.
 *
 * @return  true when the provenance row was inserted.
 */
bool insertProcessingRun(
    Database &db,
    const ToolOptions &options,
    const QString &inputHash,
    const QString &processor,
    qint64 calibrationEpoch,
    const QJsonArray &sourceTables,
    const QJsonArray &outputTables,
    QString &error)
{
    QJsonObject calibrationSource;
    calibrationSource.insert(QStringLiteral("table"), QStringLiteral("Calibration"));
    calibrationSource.insert(QStringLiteral("epoch"), calibrationEpoch);

    QJsonObject configuration;
    configuration.insert(QStringLiteral("processor"), processor);
    configuration.insert(QStringLiteral("algorithm_version"), kProcessorVersion);
    configuration.insert(QStringLiteral("calibration_source"), calibrationSource);
    if (processor == QStringLiteral("compass-orientation")) {
        configuration.insert(QStringLiteral("orientation_frame"), QStringLiteral("magnetic-frame-nwu"));
        configuration.insert(QStringLiteral("quaternion_order"), QStringLiteral("wxyz"));
        configuration.insert(
            QStringLiteral("heading_policy"),
            QStringLiteral("not_materialized; downstream tools apply declination and mounting convention to yaw"));
    }

    Statement stmt(
        db,
        "INSERT INTO ProcessingRun ("
        "ToolName, ToolVersion, ProcessorId, ProcessorVersion, CreatedUtc, "
        "InputFileName, InputSha256, ConfigurationJson, SourceTablesJson, "
        "OutputTablesJson, Status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt.valid()) {
        error = stmt.lastError();
        return false;
    }

    const QString createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (!stmt.bindText(1, QStringLiteral("dataprocessing"))
        || !stmt.bindText(2, QCoreApplication::applicationVersion())
        || !stmt.bindText(3, processor)
        || !stmt.bindInt64(4, kProcessorVersion)
        || !stmt.bindText(5, createdUtc)
        || !stmt.bindText(6, QFileInfo(options.inputPath).fileName())
        || !stmt.bindText(7, inputHash)
        || !stmt.bindText(8, jsonCompact(configuration))
        || !stmt.bindText(9, jsonCompact(sourceTables))
        || !stmt.bindText(10, jsonCompact(outputTables))
        || !stmt.bindText(11, QStringLiteral("success"))
        || !stmt.stepDone()) {
        error = stmt.lastError();
        return false;
    }
    return true;
}

QStringList streamIdsForProcessor(const QString &processor)
{
    if (processor == QStringLiteral("compass-calibrated")) {
        return {
            QStringLiteral("compass_calibrated_ax"),
            QStringLiteral("compass_calibrated_ay"),
            QStringLiteral("compass_calibrated_az"),
            QStringLiteral("compass_calibrated_mx"),
            QStringLiteral("compass_calibrated_my"),
            QStringLiteral("compass_calibrated_mz"),
        };
    }
    if (processor == QStringLiteral("compass-orientation")) {
        return {
            QStringLiteral("compass_orientation_yaw"),
            QStringLiteral("compass_orientation_pitch"),
            QStringLiteral("compass_orientation_roll"),
            QStringLiteral("compass_orientation_dip"),
            QStringLiteral("compass_orientation_field"),
            QStringLiteral("compass_orientation_acceleration"),
        };
    }
    return {};
}

QStringList tableNamesForProcessor(const QString &processor)
{
    if (processor == QStringLiteral("compass-calibrated")) {
        return {QStringLiteral("CompassCalibrated")};
    }
    if (processor == QStringLiteral("compass-orientation")) {
        return {QStringLiteral("CompassOrientation")};
    }
    return {};
}

/**
 * @brief   Applies the selected overwrite policy to a processor's outputs.
 *
 * @param[in,out] db   Open processed database inside the write transaction.
 * @param[in] processor Processor id whose tables and streams are being prepared.
 * @param[in] policy   User-selected conflict policy.
 * @param[out] skip    Set when @c IfExists::Keep leaves existing output intact.
 * @param[out] error   Populated with the conflict or SQLite failure reason.
 *
 * @return  true when output names are ready for writing or should be skipped.
 */
bool prepareProcessorOutputs(
    Database &db,
    const QString &processor,
    IfExists policy,
    bool &skip,
    QString &error)
{
    skip = false;
    bool conflict = false;
    for (const QString &table : tableNamesForProcessor(processor)) {
        bool exists = false;
        if (!tableExists(db, table, exists, error)) {
            return false;
        }
        conflict = conflict || exists;
    }
    for (const QString &id : streamIdsForProcessor(processor)) {
        bool exists = false;
        if (!streamExists(db, id, exists, error)) {
            return false;
        }
        conflict = conflict || exists;
    }

    if (conflict && policy == IfExists::Fail) {
        error = QStringLiteral("Output for %1 already exists; use --if-exists replace or keep")
                    .arg(processor);
        return false;
    }
    if (conflict && policy == IfExists::Keep) {
        skip = true;
        return true;
    }

    if (policy == IfExists::Replace || conflict) {
        for (const QString &table : tableNamesForProcessor(processor)) {
            const QString sql = QStringLiteral("DROP TABLE IF EXISTS %1").arg(table);
            const QByteArray utf8 = sql.toUtf8();
            if (!db.exec(utf8.constData(), error)) {
                return false;
            }
        }
        if (!deleteStreamIds(db, streamIdsForProcessor(processor), error)) {
            return false;
        }
    }
    return true;
}

QVector<StreamDefinition> calibratedStreamDefinitions()
{
    return {
        {"compass_calibrated_ax", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "ax", "record_column", "Acceleration X",
         "mg", "acceleration_x", "Raw accelerometer X copied beside calibrated magnetometer values."},
        {"compass_calibrated_ay", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "ay", "record_column", "Acceleration Y",
         "mg", "acceleration_y", "Raw accelerometer Y copied beside calibrated magnetometer values."},
        {"compass_calibrated_az", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "az", "record_column", "Acceleration Z",
         "mg", "acceleration_z", "Raw accelerometer Z copied beside calibrated magnetometer values."},
        {"compass_calibrated_mx", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "mx", "record_column", "Magnetic field X",
         "uT", "magnetic_field_x", "Calibrated magnetometer X sample."},
        {"compass_calibrated_my", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "my", "record_column", "Magnetic field Y",
         "uT", "magnetic_field_y", "Calibrated magnetometer Y sample."},
        {"compass_calibrated_mz", "compass_calibrated", "Compass calibrated",
         "CompassCalibrated", "Epoch", "mz", "record_column", "Magnetic field Z",
         "uT", "magnetic_field_z", "Calibrated magnetometer Z sample."},
    };
}

QVector<StreamDefinition> orientationStreamDefinitions()
{
    return {
        {"compass_orientation_yaw", nullptr, nullptr, "CompassOrientation", "Epoch", "yaw",
         "scalar", "Magnetic yaw", "deg", "angle",
         "Canonical magnetic-frame yaw. Apply declination and mounting convention downstream for display heading."},
        {"compass_orientation_pitch", nullptr, nullptr, "CompassOrientation", "Epoch", "pitch",
         "scalar", "Pitch", "deg", "angle", "Compass-derived pitch."},
        {"compass_orientation_roll", nullptr, nullptr, "CompassOrientation", "Epoch", "roll",
         "scalar", "Roll", "deg", "angle", "Compass-derived roll."},
        {"compass_orientation_dip", nullptr, nullptr, "CompassOrientation", "Epoch", "dip",
         "scalar", "Dip", "deg", "angle", "Magnetic dip angle."},
        {"compass_orientation_field", nullptr, nullptr, "CompassOrientation", "Epoch", "field",
         "scalar", "Magnetic field", "uT", "magnetic_field", "Calibrated magnetic field magnitude."},
        {"compass_orientation_acceleration", nullptr, nullptr, "CompassOrientation", "Epoch", "acceleration",
         "scalar", "Acceleration", "mg", "acceleration", "Acceleration magnitude."},
    };
}

/**
 * @brief   Writes calibrated CompassTag vector columns and stream metadata.
 *
 * @param[in,out] db     Open processed database inside the write transaction.
 * @param[in] options    Parsed command-line options for provenance fields.
 * @param[in] inputHash  SHA-256 digest of the original input file.
 * @param[in] input      Loaded CompassTag rows and calibrated vectors.
 * @param[out] error     Populated with SQLite error text on failure.
 *
 * @return  true when the output table, metadata, and provenance row were written.
 */
bool runCompassCalibrated(
    Database &db,
    const ToolOptions &options,
    const QString &inputHash,
    const CompassInput &input,
    QString &error)
{
    if (!db.exec(
            "CREATE TABLE CompassCalibrated ("
            "Epoch INTEGER,"
            "ax REAL,"
            "ay REAL,"
            "az REAL,"
            "mx REAL,"
            "my REAL,"
            "mz REAL"
            ");",
            error)) {
        return false;
    }

    Statement insert(
        db,
        "INSERT INTO CompassCalibrated (Epoch, ax, ay, az, mx, my, mz) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!insert.valid()) {
        error = insert.lastError();
        return false;
    }

    for (qsizetype i = 0; i < input.rawSamples.size(); i++) {
        const CompassSample &sample = input.rawSamples[i];
        const QVector3D mag = input.calibratedMag[i];
        if (!insert.bindInt64(1, sample.epoch)
            || !insert.bindDouble(2, sample.accel.x())
            || !insert.bindDouble(3, sample.accel.y())
            || !insert.bindDouble(4, sample.accel.z())
            || !insert.bindDouble(5, mag.x())
            || !insert.bindDouble(6, mag.y())
            || !insert.bindDouble(7, mag.z())
            || !insert.stepDone()) {
            error = insert.lastError();
            return false;
        }
    }

    if (!insertStreamDefinitions(db, calibratedStreamDefinitions(), error)) {
        return false;
    }
    return insertProcessingRun(
        db,
        options,
        inputHash,
        QStringLiteral("compass-calibrated"),
        input.calibrationEpoch,
        QJsonArray{QStringLiteral("Compass"), QStringLiteral("Calibration")},
        QJsonArray{QStringLiteral("CompassCalibrated")},
        error);
}

/**
 * @brief   Writes canonical CompassTag orientation columns and stream metadata.
 *
 * @param[in,out] db     Open processed database inside the write transaction.
 * @param[in] options    Parsed command-line options for provenance fields.
 * @param[in] inputHash  SHA-256 digest of the original input file.
 * @param[in] input      Loaded CompassTag rows and orientation samples.
 * @param[out] error     Populated with SQLite error text on failure.
 *
 * @return  true when the output table, metadata, and provenance row were written.
 */
bool runCompassOrientation(
    Database &db,
    const ToolOptions &options,
    const QString &inputHash,
    const CompassInput &input,
    QString &error)
{
    if (!db.exec(
            "CREATE TABLE CompassOrientation ("
            "Epoch INTEGER,"
            "yaw REAL,"
            "pitch REAL,"
            "roll REAL,"
            "dip REAL,"
            "field REAL,"
            "acceleration REAL,"
            "qw REAL,"
            "qx REAL,"
            "qy REAL,"
            "qz REAL"
            ");",
            error)) {
        return false;
    }

    Statement insert(
        db,
        "INSERT INTO CompassOrientation ("
        "Epoch, yaw, pitch, roll, dip, field, acceleration, qw, qx, qy, qz"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!insert.valid()) {
        error = insert.lastError();
        return false;
    }

    for (const CompassDerivedSample &sample : input.derived) {
        if (!insert.bindInt64(1, static_cast<qint64>(sample.epoch))
            || !insert.bindDouble(2, sample.yaw)
            || !insert.bindDouble(3, sample.pitch)
            || !insert.bindDouble(4, sample.roll)
            || !insert.bindDouble(5, sample.dip)
            || !insert.bindDouble(6, sample.field)
            || !insert.bindDouble(7, sample.mg)
            || !insert.bindDouble(8, sample.q.scalar())
            || !insert.bindDouble(9, sample.q.x())
            || !insert.bindDouble(10, sample.q.y())
            || !insert.bindDouble(11, sample.q.z())
            || !insert.stepDone()) {
            error = insert.lastError();
            return false;
        }
    }

    if (!insertStreamDefinitions(db, orientationStreamDefinitions(), error)) {
        return false;
    }
    return insertProcessingRun(
        db,
        options,
        inputHash,
        QStringLiteral("compass-orientation"),
        input.calibrationEpoch,
        QJsonArray{QStringLiteral("Compass"), QStringLiteral("Calibration")},
        QJsonArray{QStringLiteral("CompassOrientation")},
        error);
}

/**
 * @brief   Creates or reuses the output database according to conflict policy.
 *
 * @param[in] options  Parsed paths and output conflict policy.
 * @param[out] error   Populated with the filesystem failure or policy conflict.
 *
 * @return  true when @c options.outputPath is ready for writing.
 *
 * @post    For @c IfExists::Replace and missing outputs, the output path is a
 *          byte-for-byte copy of the input before processor writes begin.
 */
bool copyInputToOutput(const ToolOptions &options, QString &error)
{
    const QFileInfo input(options.inputPath);
    const QFileInfo output(options.outputPath);
    if (input.absoluteFilePath() == output.absoluteFilePath()) {
        error = QStringLiteral("Output path must be different from input path.");
        return false;
    }
    if (!input.exists()) {
        error = QStringLiteral("Input file does not exist: %1").arg(options.inputPath);
        return false;
    }

    if (output.exists()) {
        if (options.ifExists == IfExists::Fail) {
            error = QStringLiteral("Output file already exists: %1").arg(options.outputPath);
            return false;
        }
        if (options.ifExists == IfExists::Keep) {
            return true;
        }
        if (!QFile::remove(output.absoluteFilePath())) {
            error = QStringLiteral("Could not remove existing output file: %1").arg(options.outputPath);
            return false;
        }
    }

    if (!QFile::copy(input.absoluteFilePath(), output.absoluteFilePath())) {
        error = QStringLiteral("Could not copy %1 to %2")
                    .arg(input.absoluteFilePath(), output.absoluteFilePath());
        return false;
    }
    return true;
}

/**
 * @brief   Parses the output conflict policy option.
 *
 * @param[in] text     Command-line value.
 * @param[out] policy  Parsed enum value on success.
 *
 * @return  true for @c fail, @c replace, or @c keep.
 */
bool parseIfExists(const QString &text, IfExists &policy)
{
    if (text == QStringLiteral("fail")) {
        policy = IfExists::Fail;
        return true;
    }
    if (text == QStringLiteral("replace")) {
        policy = IfExists::Replace;
        return true;
    }
    if (text == QStringLiteral("keep")) {
        policy = IfExists::Keep;
        return true;
    }
    return false;
}

/**
 * @brief   Parses DataProcessing command-line options.
 *
 * @param[in,out] app  Qt core application used by QCommandLineParser.
 *
 * @return  Parsed options. Invalid conflict policies terminate with exit code 2.
 */
ToolOptions parseOptions(QCoreApplication &app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("copy SQLite logs and materialize derived sensor streams"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption inputOption(
        QStringList{QStringLiteral("i"), QStringLiteral("input")},
        QStringLiteral("Input SQLite log path."),
        QStringLiteral("path"));
    QCommandLineOption outputOption(
        QStringList{QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output SQLite log path."),
        QStringLiteral("path"));
    QCommandLineOption processorOption(
        QStringList{QStringLiteral("p"), QStringLiteral("processor")},
        QStringLiteral("Processor to run; repeat for multiple processors."),
        QStringLiteral("id"));
    QCommandLineOption ifExistsOption(
        QStringLiteral("if-exists"),
        QStringLiteral("Output conflict policy: fail, replace, or keep."),
        QStringLiteral("policy"),
        QStringLiteral("fail"));
    QCommandLineOption dryRunOption(
        QStringLiteral("dry-run"),
        QStringLiteral("Validate inputs and print planned outputs without writing."));
    QCommandLineOption listOption(
        QStringLiteral("list-processors"),
        QStringLiteral("List processors and whether they are available for the input log."));
    QCommandLineOption describeOption(
        QStringLiteral("describe"),
        QStringLiteral("Describe one processor."),
        QStringLiteral("id"));
    QCommandLineOption summaryOption(
        QStringLiteral("print-summary"),
        QStringLiteral("Print processing summary to stdout."));

    parser.addOption(inputOption);
    parser.addOption(outputOption);
    parser.addOption(processorOption);
    parser.addOption(ifExistsOption);
    parser.addOption(dryRunOption);
    parser.addOption(listOption);
    parser.addOption(describeOption);
    parser.addOption(summaryOption);
    parser.process(app);

    ToolOptions options;
    options.inputPath = parser.value(inputOption);
    options.outputPath = parser.value(outputOption);
    options.processors = parser.values(processorOption);
    options.dryRun = parser.isSet(dryRunOption);
    options.listProcessors = parser.isSet(listOption);
    options.describeProcessor = parser.value(describeOption);
    options.printSummary = parser.isSet(summaryOption);
    if (!parseIfExists(parser.value(ifExistsOption), options.ifExists)) {
        std::cerr << "Unknown --if-exists policy: "
                  << parser.value(ifExistsOption).toStdString() << "\n";
        std::exit(2);
    }
    return options;
}

/**
 * @brief   Validates requested processor ids before opening the output database.
 *
 * @param[in] processors  Processor ids from repeated @c --processor options.
 * @param[out] error      Populated with the unknown id, if any.
 *
 * @return  true when every id is currently supported.
 */
bool validateProcessorIds(const QStringList &processors, QString &error)
{
    for (const QString &processor : processors) {
        if (!compassProcessorIds().contains(processor)) {
            error = QStringLiteral("Unknown processor %1").arg(processor);
            return false;
        }
    }
    return true;
}

/**
 * @brief   Prints supported processor ids and their availability for a log.
 *
 * @param[in,out] db  Open input SQLite database.
 */
void listProcessors(Database &db)
{
    for (const QString &processor : compassProcessorIds()) {
        QString reason;
        const bool available = compassInputsAvailable(db, reason);
        std::cout << processor.toStdString() << ": "
                  << (available ? "available" : "unavailable")
                  << " (" << reason.toStdString() << ")\n";
    }
}

/**
 * @brief   Validates processor inputs and prints planned output tables.
 *
 * @param[in,out] db  Open input SQLite database.
 * @param[in] options Parsed command-line options.
 * @param[out] error  Populated with the validation failure reason.
 *
 * @return  true when all requested processors can run on the input.
 */
bool dryRunProcessors(Database &db, const ToolOptions &options, QString &error)
{
    CompassInput input;
    if (!loadCompassInput(db, input, error)) {
        return false;
    }

    for (const QString &processor : options.processors) {
        std::cout << "Would run " << processor.toStdString() << " on "
                  << input.rawSamples.size() << " Compass rows";
        if (processor == QStringLiteral("compass-calibrated")) {
            std::cout << " -> CompassCalibrated\n";
        } else if (processor == QStringLiteral("compass-orientation")) {
            std::cout << " -> CompassOrientation\n";
        } else {
            error = QStringLiteral("Unknown processor %1").arg(processor);
            return false;
        }
    }
    return true;
}

/**
 * @brief   Copies the input database and executes requested processors.
 *
 * @param[in] options  Parsed command-line options.
 * @param[out] error   Populated with the first filesystem, input, or SQLite
 *                     failure encountered.
 *
 * @return  true when every requested processor was written and committed.
 */
bool runProcessors(const ToolOptions &options, QString &error)
{
    QString inputHash = sha256File(options.inputPath, error);
    if (inputHash.isEmpty()) {
        return false;
    }
    if (!copyInputToOutput(options, error)) {
        return false;
    }

    Database db(options.outputPath, SQLITE_OPEN_READWRITE);
    if (!db.isOpen()) {
        error = db.lastError();
        return false;
    }

    CompassInput input;
    if (!loadCompassInput(db, input, error)) {
        return false;
    }

    if (!db.exec("BEGIN IMMEDIATE TRANSACTION", error)) {
        return false;
    }
    if (!createProcessingRunTable(db, error)) {
        db.exec("ROLLBACK", error);
        return false;
    }

    for (const QString &processor : options.processors) {
        bool skip = false;
        if (!prepareProcessorOutputs(db, processor, options.ifExists, skip, error)) {
            db.exec("ROLLBACK", error);
            return false;
        }
        if (skip) {
            if (options.printSummary) {
                std::cout << "Kept existing outputs for " << processor.toStdString() << "\n";
            }
            continue;
        }

        bool ok = false;
        if (processor == QStringLiteral("compass-calibrated")) {
            ok = runCompassCalibrated(db, options, inputHash, input, error);
        } else if (processor == QStringLiteral("compass-orientation")) {
            ok = runCompassOrientation(db, options, inputHash, input, error);
        } else {
            error = QStringLiteral("Unknown processor %1").arg(processor);
        }
        if (!ok) {
            db.exec("ROLLBACK", error);
            return false;
        }
        if (options.printSummary) {
            std::cout << "Wrote " << processor.toStdString() << "\n";
        }
    }

    return db.exec("COMMIT", error);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("dataprocessing"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    const ToolOptions options = parseOptions(app);
    QString error;

    if (!options.describeProcessor.isEmpty()) {
        const QString description = processorDescription(options.describeProcessor);
        if (description.isEmpty()) {
            std::cerr << "Unknown processor " << options.describeProcessor.toStdString() << "\n";
            return 2;
        }
        std::cout << options.describeProcessor.toStdString() << "\n"
                  << description.toStdString() << "\n";
        return 0;
    }

    if (options.inputPath.isEmpty()) {
        std::cerr << "--input is required\n";
        return 2;
    }

    Database inputDb(options.inputPath, SQLITE_OPEN_READONLY);
    if (!inputDb.isOpen()) {
        std::cerr << "Could not open input: " << inputDb.lastError().toStdString() << "\n";
        return 1;
    }

    if (options.listProcessors) {
        listProcessors(inputDb);
        return 0;
    }

    if (options.processors.isEmpty()) {
        std::cerr << "At least one --processor is required unless using --list-processors or --describe\n";
        return 2;
    }
    if (!validateProcessorIds(options.processors, error)) {
        std::cerr << error.toStdString() << "\n";
        return 2;
    }

    if (options.dryRun) {
        if (!dryRunProcessors(inputDb, options, error)) {
            std::cerr << error.toStdString() << "\n";
            return 1;
        }
        return 0;
    }

    if (options.outputPath.isEmpty()) {
        std::cerr << "--output is required when writing processors\n";
        return 2;
    }

    if (!runProcessors(options, error)) {
        std::cerr << error.toStdString() << "\n";
        return 1;
    }

    if (options.printSummary) {
        std::cout << "Wrote processed log " << options.outputPath.toStdString() << "\n";
    }
    return 0;
}
