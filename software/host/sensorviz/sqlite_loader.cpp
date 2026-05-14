#include "sqlite_loader.h"

#include <sqlite3.h>

// SQLite logs from different tag types share a small set of table conventions,
// but no single tag is expected to write every table. This file is the adapter
// from that sparse on-disk schema to sensorViz's uniform SensorStream model.
//
// To add support for a new table:
// - confirm the table has an Epoch column and one numeric value column
// - add one loadStream() call in SqliteLoader::load()
// - choose a stable stream id; controls.cpp uses ids for transforms and ranges
//
// If a table needs multiple value columns or special decoding, add a sibling to
// loadStream() here and still return one or more SensorStream objects.
namespace
{

// Small RAII wrappers keep the SQLite C API localized to this file. sensorViz
// only needs read-only queries, so these wrappers intentionally stay minimal.
class Database
{
public:
    explicit Database(const QString &path)
    {
        const QByteArray utf8_path = path.toUtf8();
        if (sqlite3_open_v2(utf8_path.constData(), &db_, SQLITE_OPEN_READONLY, nullptr)
            != SQLITE_OK) {
            last_error_ = errorString();
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
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
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
    QString last_error_;
};

class Statement
{
public:
    Statement(Database &db, const char *sql) : db_(db.get())
    {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            last_error_ = errorString();
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

    bool next()
    {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc != SQLITE_DONE) {
            last_error_ = errorString();
        }
        return false;
    }

    qint64 int64Column(int column) const
    {
        return sqlite3_column_int64(stmt_, column);
    }

    double doubleColumn(int column) const
    {
        return sqlite3_column_double(stmt_, column);
    }

    QString textColumn(int column) const
    {
        const unsigned char *text = sqlite3_column_text(stmt_, column);
        return QString::fromUtf8(reinterpret_cast<const char *>(text ? text : (const unsigned char *)""));
    }

    QString lastError() const
    {
        if (!last_error_.isEmpty()) {
            return last_error_;
        }
        return errorString();
    }

private:
    QString errorString() const
    {
        return QString::fromUtf8(db_ ? sqlite3_errmsg(db_) : "database is not open");
    }

    sqlite3 *db_ = nullptr;
    sqlite3_stmt *stmt_ = nullptr;
    QString last_error_;
};

bool tableExistsRaw(Database &db, const QString &table_name)
{
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT 1 FROM sqlite_master WHERE type='table' AND lower(name)=lower(?)";
    if (sqlite3_prepare_v2(db.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    const QByteArray utf8_name = table_name.toUtf8();
    sqlite3_bind_text(stmt, 1, utf8_name.constData(), -1, SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return exists;
}

// Convert one optional SQLite table into a SensorStream. A missing table is not
// an error because each tag type writes a different subset of sensor tables.
// A present-but-invalid table is an error because that usually means the log
// was produced with a schema this viewer does not understand yet.
bool loadStream(
    Database &db,
    const QString &table_name,
    const QString &value_column,
    const QString &id,
    const QString &label,
    const QString &units,
    const QColor &color,
    SensorLog &log,
    QString &error)
{
    if (!tableExistsRaw(db, table_name)) {
        return true;
    }

    const QString sql = QString("SELECT Epoch, %1 FROM %2 ORDER BY Epoch")
                            .arg(value_column, table_name);
    const QByteArray utf8_sql = sql.toUtf8();
    Statement stmt(db, utf8_sql.constData());
    if (!stmt.valid()) {
        error = QString("Failed to load %1: %2").arg(label, stmt.lastError());
        return false;
    }

    SensorStream stream;
    stream.id = id;
    stream.label = label;
    stream.units = units;
    stream.color = color;

    while (stmt.next()) {
        stream.time.append(stmt.int64Column(0));
        stream.value.append(stmt.doubleColumn(1));
    }

    // Empty tables do not contribute menu items or axes.
    if (stream.time.isEmpty()) {
        return true;
    }

    log.streams.append(stream);
    return true;
}

} // namespace

bool SqliteLoader::load(const QString &path, SensorLog &log, QString &error)
{
    Database db(path);
    if (!db.isOpen()) {
        error = "Could not open database: " + db.lastError();
        return false;
    }

    SensorLog loaded;
    loaded.path = path;

    // Preserve the info table for the File Info tab and use tagtype only as
    // metadata. Feature availability is inferred from actual tables/streams.
    Statement info_query(db, "SELECT fieldname, value FROM info");
    if (info_query.valid()) {
        while (info_query.next()) {
            const QString field = info_query.textColumn(0);
            const QString value = info_query.textColumn(1);
            loaded.info.insert(field, value);
            if (field.compare("tagtype", Qt::CaseInsensitive) == 0) {
                loaded.tagType = value;
            }
        }
    }

    // Known stream tables across the currently supported sensor tags. Adding a
    // future sensor type should usually mean adding another loadStream entry
    // here; the rest of the UI will pick it up from SensorLog::streams.
    if (!loadStream(db, "Activity", "Activity", "activity", "Activity", "%", QColor(30, 90, 180), loaded, error)
        || !loadStream(db, "Pressure", "Pressure", "pressure", "Pressure", "mbar", QColor(25, 130, 105), loaded, error)
        || !loadStream(db, "Temperature", "Temperature", "sensor_temperature", "Sensor temperature", "C", QColor(210, 95, 35), loaded, error)
        || !loadStream(db, "CoreTemperature", "Temperature", "core_temperature", "Core temperature", "C", QColor(180, 55, 55), loaded, error)
        || !loadStream(db, "Voltage", "Voltage", "voltage", "Voltage", "V", QColor(60, 145, 75), loaded, error)) {
        return false;
    }

    if (loaded.streams.isEmpty()) {
        error = "No supported sensor streams found in database";
        return false;
    }

    log = loaded;
    return true;
}
