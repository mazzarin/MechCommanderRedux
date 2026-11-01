#include "stdafx.h"
#include "DaoDatabase.h"
#include "FitAblSchema.h"
#include "JetDatabase.h"
#include <sqlite3.h>

namespace
{
    /// <summary>FitAbl.mdb's code page (its text columns say 0x4e4).</summary>
    constexpr UINT JET_CODE_PAGE = 1252;

    /// <summary>The SQLite file for the database named <paramref name="name"/>: "X.mdb" becomes "X.db".</summary>
    CString databaseFile(const CString& name)
    {
        CString path = name;

        if (path.GetLength() >= 4 && path.Right(4).CompareNoCase(".mdb") == 0)
        {
            path = path.Left(path.GetLength() - 4);
        }

        return path + ".db";
    }

    /// <summary>SQLite's message for the last error on <paramref name="db"/>.</summary>
    CString lastError(sqlite3* db)
    {
        return FromUtf8(sqlite3_errmsg(db));
    }

    /// <summary>Runs <paramref name="sql"/> on <paramref name="db"/>.</summary>
    /// <returns>False, with <paramref name="error"/>, when it fails.</returns>
    bool execute(sqlite3* db, const char* sql, CString& error)
    {
        char* message = nullptr;

        if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK)
        {
            error = message != nullptr ? FromUtf8(message) : lastError(db);
            sqlite3_free(message);
            return false;
        }

        return true;
    }

    /// <summary>
    /// <paramref name="identifier"/> as an SQL quoted identifier: in double quotes, each one inside doubled (issue #25: the
    /// .mdb's column names went into brackets as they were, and a ']' in one ended the name).
    /// </summary>
    CString quoteIdentifier(const std::string& identifier)
    {
        std::string quoted = "\"";

        for (const char c : identifier)
        {
            quoted += c;

            if (c == '"')
            {
                quoted += '"';
            }
        }

        quoted += '"';
        return CString(quoted.c_str());
    }

    /// <summary>Whether <paramref name="db"/>'s table <paramref name="table"/> has the column <paramref name="column"/>.</summary>
    bool hasColumn(sqlite3* db, const char* table, const char* column)
    {
        // PRAGMA table_info lists a table's columns, a row each, its name the second value.
        const CString sql = "PRAGMA table_info(" + quoteIdentifier(table) + ")";
        sqlite3_stmt* columns = nullptr;
        bool found = false;

        if (sqlite3_prepare_v2(db, ToUtf8(sql).c_str(), -1, &columns, nullptr) == SQLITE_OK)
        {
            while (!found && sqlite3_step(columns) == SQLITE_ROW)
            {
                const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(columns, 1));
                found = name != nullptr && _stricmp(name, column) == 0;
            }
        }

        sqlite3_finalize(columns);
        return found;
    }

    /// <summary>
    /// Port-only: adds the port's own columns (FITABL_PORT_COLUMNS) that <paramref name="db"/> lacks, a database made
    /// before them, with their defaults.
    /// </summary>
    /// <returns>False, with <paramref name="error"/>, when one can't be added.</returns>
    bool addPortColumns(sqlite3* db, CString& error)
    {
        for (const FitAblPortColumn& column : FITABL_PORT_COLUMNS)
        {
            if (hasColumn(db, column.table, column.column))
            {
                continue;
            }

            const CString sql = "ALTER TABLE " + quoteIdentifier(column.table) + " ADD COLUMN " +
                                quoteIdentifier(column.column) + " " + column.definition + ";";

            if (!execute(db, ToUtf8(sql).c_str(), error))
            {
                error = CString("Adding ") + column.table + "." + column.column + ": " + error;
                return false;
            }
        }

        return true;
    }

    /// <summary>Copies the rows of the Jet table <paramref name="name"/> into the SQLite table of that name.</summary>
    bool importTable(const JetDatabase& jet, sqlite3* db, const char* name, CString& error)
    {
        JetTable table;

        if (!jet.readTable(name, table, error))
        {
            return false;
        }

        // Each Jet column goes to the SQLite column of its name; there is one for each (the schema is FitAbl.mdb's).
        CString sql;
        CString values;
        sql = "INSERT INTO " + quoteIdentifier(name) + " (";

        for (size_t c = 0; c < table.columns.size(); ++c)
        {
            sql += (c == 0 ? "" : ", ") + quoteIdentifier(table.columns[c].name);
            values += c == 0 ? "?" : ", ?";
        }

        sql += ") VALUES (" + values + ")";
        sqlite3_stmt* insert = nullptr;

        if (sqlite3_prepare_v2(db, ToUtf8(sql).c_str(), -1, &insert, nullptr) != SQLITE_OK)
        {
            error.Format("Table %s: %s", name, (LPCTSTR)lastError(db));
            return false;
        }

        int64_t lastKey = 0;

        for (const std::vector<JetValue>& row : table.rows)
        {
            sqlite3_reset(insert);

            for (size_t c = 0; c < row.size(); ++c)
            {
                const int index = static_cast<int>(c) + 1;
                const JetValue& value = row[c];

                switch (value.kind)
                {
                    case JetValue::Kind::Null:
                        sqlite3_bind_null(insert, index);
                        break;
                    case JetValue::Kind::Integer:
                        sqlite3_bind_int64(insert, index, value.integer);
                        break;
                    case JetValue::Kind::Real:
                        sqlite3_bind_double(insert, index, value.real);
                        break;
                    case JetValue::Kind::Text:
                    {
                        const std::string text = ToUtf8(value.text.c_str(), JET_CODE_PAGE);
                        sqlite3_bind_text(insert, index, text.c_str(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
                        break;
                    }
                }
            }

            if (sqlite3_step(insert) != SQLITE_DONE)
            {
                error.Format("Table %s: %s", name, (LPCTSTR)lastError(db));
                sqlite3_finalize(insert);
                return false;
            }

            lastKey = std::max<int64_t>(lastKey, sqlite3_last_insert_rowid(db));
        }

        sqlite3_finalize(insert);

        // An AutoNumber table goes on from Jet's counter (the last number given), as the .mdb would have.
        sqlite3_stmt* sequence = nullptr;
        const char* const SEQUENCE_SQL = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1 AND "
                                         "sql LIKE '%AUTOINCREMENT%'";

        if (sqlite3_prepare_v2(db, SEQUENCE_SQL, -1, &sequence, nullptr) != SQLITE_OK)
        {
            error = lastError(db);
            return false;
        }

        sqlite3_bind_text(sequence, 1, name, -1, SQLITE_TRANSIENT);
        const bool autoNumber = sqlite3_step(sequence) == SQLITE_ROW;
        sqlite3_finalize(sequence);

        if (autoNumber)
        {
            CString update;
            update.Format("DELETE FROM sqlite_sequence WHERE name = '%s'; INSERT INTO sqlite_sequence (name, seq) "
                          "VALUES ('%s', %lld);",
                          name, name, std::max<int64_t>(lastKey, table.autonumber));

            if (!execute(db, update, error))
            {
                return false;
            }
        }

        return true;
    }
} // namespace

IMPLEMENT_DYNAMIC(DaoException, CException)

DaoException::DaoException(const CString& description, int code) : m_strDescription(description), m_nCode(code)
{
}

BOOL DaoException::GetErrorMessage(LPTSTR lpszError, UINT nMaxError, PUINT pnHelpContext) const
{
    if (pnHelpContext != nullptr)
    {
        *pnHelpContext = 0;
    }

    if (lpszError == nullptr || nMaxError == 0)
    {
        return FALSE;
    }

    Checked::tcsncpy_s(lpszError, nMaxError, m_strDescription, _TRUNCATE);
    return TRUE;
}

void ThrowDaoException(const CString& description, int code)
{
    throw new DaoException(description, code);
}

std::string ToUtf8(const char* text, UINT codePage)
{
    const int wideLength = MultiByteToWideChar(codePage, 0, text, -1, nullptr, 0);

    if (wideLength <= 1)
    {
        return std::string();
    }

    std::wstring wide(wideLength, L'\0');
    MultiByteToWideChar(codePage, 0, text, -1, wide.data(), wideLength);
    const int length = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), length, nullptr, nullptr);
    utf8.resize(length - 1);
    return utf8;
}

CString FromUtf8(const char* text)
{
    const int wideLength = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);

    if (wideLength <= 1)
    {
        return CString();
    }

    std::wstring wide(wideLength, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), wideLength);
    return CString(wide.c_str());
}

DaoDatabase::~DaoDatabase()
{
    Close();
}

bool DaoDatabase::Import(const CString& mdbPath, const CString& dbPath, CString& error)
{
    JetDatabase jet;

    if (!jet.open(mdbPath, error))
    {
        return false;
    }

    // Built under a temporary name and renamed when complete, so a failed import leaves nothing to be opened.
    const CString building = dbPath + ".new";
    DeleteFile(building);
    sqlite3* db = nullptr;

    if (sqlite3_open_v2(ToUtf8(building).c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK)
    {
        error.Format("%s can't be made: %s", (LPCTSTR)building, db != nullptr ? (LPCTSTR)lastError(db) : "");
        sqlite3_close_v2(db);
        return false;
    }

    bool ok = execute(db, "PRAGMA foreign_keys = ON; BEGIN;", error) && execute(db, FITABL_SCHEMA, error);

    for (const FitAblTable& table : FITABL_TABLES)
    {
        if (!ok)
        {
            break;
        }

        ok = importTable(jet, db, table.name, error);
    }

    ok = ok && execute(db, "COMMIT;", error);
    sqlite3_close_v2(db);

    if (ok && !MoveFileEx(building, dbPath, MOVEFILE_REPLACE_EXISTING))
    {
        error.Format("%s can't be renamed to %s.", (LPCTSTR)building, (LPCTSTR)dbPath);
        ok = false;
    }

    if (!ok)
    {
        CString context;
        context.Format("Importing %s: ", (LPCTSTR)mdbPath);
        error = context + error;
        DeleteFile(building);
    }

    return ok;
}

void DaoDatabase::Open(LPCTSTR lpszName, BOOL bExclusive, BOOL bReadOnly, LPCTSTR lpszConnect)
{
    UNREFERENCED_PARAMETER(bExclusive);
    UNREFERENCED_PARAMETER(lpszConnect);

    if (m_bOpen)
    {
        Close();
    }

    const CString path = databaseFile(lpszName);

    if (GetFileAttributes(path) == INVALID_FILE_ATTRIBUTES)
    {
        CString error;

        if (GetFileAttributes(lpszName) == INVALID_FILE_ATTRIBUTES)
        {
            error.Format("Couldn't find file '%s'.", lpszName);
            ThrowDaoException(error);
        }

        if (!Import(lpszName, path, error))
        {
            ThrowDaoException(error);
        }
    }

    const int flags = bReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE;

    if (sqlite3_open_v2(ToUtf8(path).c_str(), &m_pConnection, flags, nullptr) != SQLITE_OK)
    {
        const CString error = m_pConnection != nullptr ? lastError(m_pConnection) : CString("out of memory");
        const int code = m_pConnection != nullptr ? sqlite3_errcode(m_pConnection) : SQLITE_NOMEM;
        sqlite3_close_v2(m_pConnection);
        m_pConnection = nullptr;
        ThrowDaoException(path + ": " + error, code);
    }

    // Jet enforced the relationships; SQLite does when asked, on each connection.
    sqlite3_exec(m_pConnection, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    if (CString error; !bReadOnly && !addPortColumns(m_pConnection, error))
    {
        const int code = sqlite3_errcode(m_pConnection);
        sqlite3_close_v2(m_pConnection);
        m_pConnection = nullptr;
        ThrowDaoException(path + ": " + error, code);
    }

    m_strName = lpszName;
    m_bOpen = TRUE;
}

void DaoDatabase::Close()
{
    if (m_pConnection != nullptr)
    {
        sqlite3_close_v2(m_pConnection);
        m_pConnection = nullptr;
    }

    m_bOpen = FALSE;
}

BOOL DaoDatabase::IsOpen() const
{
    return m_bOpen;
}

CString DaoDatabase::GetName() const
{
    return m_strName;
}

void DaoDatabase::Execute(LPCTSTR lpszSQL, int nOptions)
{
    UNREFERENCED_PARAMETER(nOptions);
    CString error;

    if (!m_bOpen)
    {
        ThrowDaoException("The database isn't open.");
    }

    if (!execute(m_pConnection, ToUtf8(lpszSQL).c_str(), error))
    {
        ThrowDaoException(error, sqlite3_errcode(m_pConnection));
    }
}

sqlite3* DaoDatabase::GetHandle() const
{
    return m_pConnection;
}
