#ifndef COMPVIZ_SQLITE_UTILS_H
#define COMPVIZ_SQLITE_UTILS_H

#include <QString>

#include <sqlite3.h>

class SqliteDatabase
{
public:
    explicit SqliteDatabase(const QString &path)
    {
        const QByteArray utf8Path = path.toUtf8();
        if (sqlite3_open_v2(
                utf8Path.constData(), &db_, SQLITE_OPEN_READONLY, nullptr)
            != SQLITE_OK) {
            lastError_ = errorString();
            close();
        }
    }

    ~SqliteDatabase()
    {
        close();
    }

    SqliteDatabase(const SqliteDatabase &) = delete;
    SqliteDatabase &operator=(const SqliteDatabase &) = delete;

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
        if (!lastError_.isEmpty()) {
            return lastError_;
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
    QString lastError_;
};

class SqliteStatement
{
public:
    SqliteStatement(SqliteDatabase &db, const char *sql) : db_(db.get())
    {
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr) != SQLITE_OK) {
            lastError_ = errorString();
            stmt_ = nullptr;
        }
    }

    ~SqliteStatement()
    {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
    }

    SqliteStatement(const SqliteStatement &) = delete;
    SqliteStatement &operator=(const SqliteStatement &) = delete;

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
            lastError_ = errorString();
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
        return QString::fromUtf8(
            reinterpret_cast<const char *>(text ? text : (const unsigned char *)""));
    }

    QString lastError() const
    {
        if (!lastError_.isEmpty()) {
            return lastError_;
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
    QString lastError_;
};

#endif // COMPVIZ_SQLITE_UTILS_H
