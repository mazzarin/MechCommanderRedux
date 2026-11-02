#pragma once

// The port's own: the SQLite schema of the mission database, FitAbl.db, which replaces the original's Jet database
// Teditor\FitAbl.mdb.

/// <summary>A table of the mission database.</summary>
struct FitAblTable
{
    /// <summary>The table's name, as in FitAbl.mdb.</summary>
    const char* name;
};

/// <summary>The number of tables in the mission database.</summary>
inline constexpr size_t FITABL_TABLE_COUNT = 12;

/// <summary>A column whose Jet type its SQLite storage doesn't give.</summary>
struct FitAblColumnType
{
    /// <summary>The table's name.</summary>
    const char* table;
    /// <summary>The column's name.</summary>
    const char* column;
    /// <summary>The variant type DAO reads it as.</summary>
    VARTYPE type;
};

/// <summary>The number of columns in <see cref="FITABL_COLUMN_TYPES"/>.</summary>
inline constexpr size_t FITABL_COLUMN_TYPE_COUNT = 18;

/// <summary>A column of the port's own, beyond FitAbl.mdb's: added to a database that lacks it as it opens.</summary>
struct FitAblPortColumn
{
    /// <summary>The table's name.</summary>
    const char* table;
    /// <summary>The column's name.</summary>
    const char* column;
    /// <summary>Its type and default, as ALTER TABLE ... ADD COLUMN takes them.</summary>
    const char* definition;
};

/// <summary>The number of columns in <see cref="FITABL_PORT_COLUMNS"/>.</summary>
inline constexpr size_t FITABL_PORT_COLUMN_COUNT = 2;

/// <summary>The SQL that creates the mission database's tables and indexes.</summary>
extern const char FITABL_SCHEMA[];
/// <summary>
/// The port's own columns: NewMissions' TimeOfDay (MissionTimeOfDay.h) and Weather (MissionWeather.h). Not in
/// FITABL_SCHEMA, which is FitAbl.mdb's: DaoDatabase::Open adds them.
/// </summary>
extern const std::array<FitAblPortColumn, FITABL_PORT_COLUMN_COUNT> FITABL_PORT_COLUMNS;
/// <summary>The tables, parents before the tables whose foreign keys refer to them.</summary>
extern const std::array<FitAblTable, FITABL_TABLE_COUNT> FITABL_TABLES;
/// <summary>
/// The Yes/No, Byte, Integer, Single and Date/Time columns: the others are Long when stored as INTEGER, Double as
/// REAL and Text as TEXT.
/// </summary>
extern const std::array<FitAblColumnType, FITABL_COLUMN_TYPE_COUNT> FITABL_COLUMN_TYPES;

/// <summary>
/// The variant type DAO reads column <paramref name="column"/> of <paramref name="table"/> as, when it isn't the
/// default of its storage; else VT_EMPTY.
/// </summary>
VARTYPE FitAblColumnVarType(const char* table, const char* column);
