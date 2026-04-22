#include "core/database.h"
#include "core/pinyin.h"
#include "sqlite3.h"
#include <iostream>
#include <filesystem>
#include <regex>

namespace fastsearch {

// ── helpers for advanced filtering ──

static std::string build_filter_sql(const SearchOptions& opts, const std::string& prefix = "") {
    std::string sql;
    std::string p = prefix.empty() ? "" : prefix + ".";
    if (!opts.ext_filter.empty()) sql += " AND " + p + "ext=?";
    if (!opts.path_filter.empty()) sql += " AND " + p + "path LIKE ?";
    if (opts.type_filter >= 0) sql += " AND " + p + "is_dir=" + std::to_string(opts.type_filter);
    if (opts.min_size >= 0) sql += " AND " + p + "size>=" + std::to_string(opts.min_size);
    if (opts.max_size >= 0) sql += " AND " + p + "size<=" + std::to_string(opts.max_size);
    if (opts.min_time >= 0) sql += " AND " + p + "modified>=" + std::to_string(opts.min_time);
    if (opts.max_time >= 0) sql += " AND " + p + "modified<=" + std::to_string(opts.max_time);
    return sql;
}

static std::string build_order_sql(const SearchOptions& opts, const std::string& prefix = "") {
    std::string p = prefix.empty() ? "" : prefix + ".";
    if (!opts.sort_by.empty()) {
        std::string col = p + "name";
        if (opts.sort_by == "size") col = p + "size";
        else if (opts.sort_by == "modified") col = p + "modified";
        std::string dir = (opts.sort_order == "desc") ? "DESC" : "ASC";
        return " ORDER BY " + col + " " + dir;
    }
    return " ORDER BY " + p + "is_dir DESC, " + p + "name ASC";
}

static void bind_filters(sqlite3_stmt* stmt, int& idx, const SearchOptions& opts) {
    if (!opts.ext_filter.empty())
        sqlite3_bind_text(stmt, idx++, opts.ext_filter.c_str(), -1, SQLITE_TRANSIENT);
    if (!opts.path_filter.empty()) {
        std::string plike = opts.path_filter + "%";
        sqlite3_bind_text(stmt, idx++, plike.c_str(), -1, SQLITE_TRANSIENT);
    }
}

// ── Database implementation ──

Database::Database() = default;
Database::~Database() { close(); }

bool Database::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) close();
    db_path_ = path;

    int rc = sqlite3_open(path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB] Failed to open: " << sqlite3_errmsg(db_) << "\n";
        db_ = nullptr;
        return false;
    }

    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA cache_size=-8000");
    exec("PRAGMA temp_store=MEMORY");

    sqlite3_create_function(db_, "REGEXP", 2, SQLITE_UTF8, nullptr,
        [](sqlite3_context* ctx, int, sqlite3_value** argv) {
            const char* pattern = reinterpret_cast<const char*>(sqlite3_value_text(argv[0]));
            const char* text = reinterpret_cast<const char*>(sqlite3_value_text(argv[1]));
            if (!pattern || !text) { sqlite3_result_int(ctx, 0); return; }
            try {
                std::regex re(pattern, std::regex::icase | std::regex::optimize);
                sqlite3_result_int(ctx, std::regex_search(text, re) ? 1 : 0);
            } catch (...) {
                sqlite3_result_int(ctx, 0);
            }
        }, nullptr, nullptr);

    return true;
}

void Database::close() {
    if (db_) { sqlite3_close(db_); db_ = nullptr; }
}

bool Database::is_open() const { return db_ != nullptr; }

bool Database::create_tables() {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS files (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT NOT NULL,
            path        TEXT NOT NULL UNIQUE,
            parent_dir  TEXT NOT NULL,
            size        INTEGER DEFAULT 0,
            modified    INTEGER DEFAULT 0,
            is_dir      INTEGER DEFAULT 0,
            ext         TEXT DEFAULT '',
            name_pinyin TEXT DEFAULT ''
        );
        CREATE INDEX IF NOT EXISTS idx_name ON files(name);
        CREATE INDEX IF NOT EXISTS idx_ext ON files(ext);
        CREATE INDEX IF NOT EXISTS idx_parent ON files(parent_dir);
        CREATE INDEX IF NOT EXISTS idx_pinyin ON files(name_pinyin);

        CREATE VIRTUAL TABLE IF NOT EXISTS files_fts USING fts5(
            name, path, content=files, content_rowid=id
        );

        CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN
            INSERT INTO files_fts(rowid, name, path) VALUES (new.id, new.name, new.path);
        END;
        CREATE TRIGGER IF NOT EXISTS files_ad AFTER DELETE ON files BEGIN
            INSERT INTO files_fts(files_fts, rowid, name, path) VALUES('delete', old.id, old.name, old.path);
        END;
        CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN
            INSERT INTO files_fts(files_fts, rowid, name, path) VALUES('delete', old.id, old.name, old.path);
            INSERT INTO files_fts(rowid, name, path) VALUES (new.id, new.name, new.path);
        END;
    )SQL";

    return exec(schema);
}

bool Database::begin_transaction() { std::lock_guard<std::mutex> lock(mutex_); return exec("BEGIN TRANSACTION"); }
bool Database::commit_transaction() { std::lock_guard<std::mutex> lock(mutex_); return exec("COMMIT"); }
bool Database::rollback_transaction() { std::lock_guard<std::mutex> lock(mutex_); return exec("ROLLBACK"); }

bool Database::insert_file(const FileRecord& rec) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* sql = "INSERT OR REPLACE INTO files(name,path,parent_dir,size,modified,is_dir,ext,name_pinyin) "
                      "VALUES(?,?,?,?,?,?,?,?)";
    auto* stmt = prepare(sql);
    if (!stmt) return false;

    std::string py = extract_pinyin_initials(rec.name);
    sqlite3_bind_text(stmt, 1, rec.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, rec.parent_dir.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, rec.size);
    sqlite3_bind_int64(stmt, 5, rec.modified);
    sqlite3_bind_int(stmt, 6, rec.is_dir ? 1 : 0);
    sqlite3_bind_text(stmt, 7, rec.ext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, py.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::insert_files_batch(const std::vector<FileRecord>& records) {
    const char* sql = "INSERT OR REPLACE INTO files(name,path,parent_dir,size,modified,is_dir,ext,name_pinyin) "
                      "VALUES(?,?,?,?,?,?,?,?)";

    std::lock_guard<std::mutex> lock(mutex_);
    auto* stmt = prepare(sql);
    if (!stmt) return false;

    for (auto& rec : records) {
        std::string py = extract_pinyin_initials(rec.name);
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        sqlite3_bind_text(stmt, 1, rec.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, rec.path.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, rec.parent_dir.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, rec.size);
        sqlite3_bind_int64(stmt, 5, rec.modified);
        sqlite3_bind_int(stmt, 6, rec.is_dir ? 1 : 0);
        sqlite3_bind_text(stmt, 7, rec.ext.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 8, py.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool Database::delete_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto* stmt = prepare("DELETE FROM files WHERE path=?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::update_file(const FileRecord& rec) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string py = extract_pinyin_initials(rec.name);
    auto* stmt = prepare("UPDATE files SET name=?,parent_dir=?,size=?,modified=?,is_dir=?,ext=?,name_pinyin=? WHERE path=?");
    if (!stmt) return false;
    sqlite3_bind_text(stmt, 1, rec.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, rec.parent_dir.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, rec.size);
    sqlite3_bind_int64(stmt, 4, rec.modified);
    sqlite3_bind_int(stmt, 5, rec.is_dir ? 1 : 0);
    sqlite3_bind_text(stmt, 6, rec.ext.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, py.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, rec.path.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

bool Database::clear_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    exec("DELETE FROM files");
    exec("INSERT INTO files_fts(files_fts) VALUES('rebuild')");
    return true;
}

std::vector<FileRecord> Database::search_by_name(const SearchOptions& opts) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FileRecord> results;

    std::string sql = "SELECT id,name,path,parent_dir,size,modified,is_dir,ext FROM files WHERE 1=1";

    if (!opts.query.empty()) {
        if (opts.regex)
            sql += " AND name REGEXP ?";
        else if (opts.query.find('*') != std::string::npos || opts.query.find('?') != std::string::npos)
            sql += " AND name GLOB ?";
        else
            sql += " AND name LIKE ?";
    }
    sql += build_filter_sql(opts);
    sql += build_order_sql(opts);
    sql += " LIMIT ? OFFSET ?";

    auto* stmt = prepare(sql);
    if (!stmt) return results;

    int idx = 1;
    if (!opts.query.empty()) {
        if (opts.regex || opts.query.find('*') != std::string::npos || opts.query.find('?') != std::string::npos)
            sqlite3_bind_text(stmt, idx++, opts.query.c_str(), -1, SQLITE_TRANSIENT);
        else {
            std::string like = "%" + opts.query + "%";
            sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
        }
    }
    bind_filters(stmt, idx, opts);
    sqlite3_bind_int(stmt, idx++, opts.limit);
    sqlite3_bind_int(stmt, idx++, opts.offset);

    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(row_to_record(stmt));
    sqlite3_finalize(stmt);
    return results;
}

std::vector<FileRecord> Database::search_fts(const SearchOptions& opts) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FileRecord> results;
    if (opts.query.empty()) return results;

    std::string sql =
        "SELECT f.id,f.name,f.path,f.parent_dir,f.size,f.modified,f.is_dir,f.ext "
        "FROM files f JOIN files_fts ft ON f.id=ft.rowid "
        "WHERE files_fts MATCH ?";
    sql += build_filter_sql(opts, "f");
    sql += " ORDER BY rank LIMIT ? OFFSET ?";

    auto* stmt = prepare(sql);
    if (!stmt) return results;

    int idx = 1;
    sqlite3_bind_text(stmt, idx++, opts.query.c_str(), -1, SQLITE_TRANSIENT);
    bind_filters(stmt, idx, opts);
    sqlite3_bind_int(stmt, idx++, opts.limit);
    sqlite3_bind_int(stmt, idx++, opts.offset);

    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(row_to_record(stmt));
    sqlite3_finalize(stmt);
    return results;
}

std::vector<FileRecord> Database::search_regex(const SearchOptions& opts) {
    SearchOptions ropts = opts;
    ropts.regex = true;
    return search_by_name(ropts);
}

std::vector<FileRecord> Database::search_pinyin(const SearchOptions& opts) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FileRecord> results;
    if (opts.query.empty()) return results;

    std::string sql = "SELECT id,name,path,parent_dir,size,modified,is_dir,ext FROM files "
                      "WHERE name_pinyin LIKE ?";
    sql += build_filter_sql(opts);
    sql += build_order_sql(opts);
    sql += " LIMIT ? OFFSET ?";

    auto* stmt = prepare(sql);
    if (!stmt) return results;

    int idx = 1;
    std::string like = "%" + opts.query + "%";
    sqlite3_bind_text(stmt, idx++, like.c_str(), -1, SQLITE_TRANSIENT);
    bind_filters(stmt, idx, opts);
    sqlite3_bind_int(stmt, idx++, opts.limit);
    sqlite3_bind_int(stmt, idx++, opts.offset);

    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(row_to_record(stmt));
    sqlite3_finalize(stmt);
    return results;
}

IndexStats Database::get_stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    IndexStats stats;

    auto* s1 = prepare("SELECT COUNT(*) FROM files WHERE is_dir=0");
    if (s1 && sqlite3_step(s1) == SQLITE_ROW) stats.total_files = sqlite3_column_int64(s1, 0);
    sqlite3_finalize(s1);

    auto* s2 = prepare("SELECT COUNT(*) FROM files WHERE is_dir=1");
    if (s2 && sqlite3_step(s2) == SQLITE_ROW) stats.total_dirs = sqlite3_column_int64(s2, 0);
    sqlite3_finalize(s2);

    namespace fs = std::filesystem;
    if (fs::exists(db_path_))
        stats.db_size_bytes = static_cast<int64_t>(fs::file_size(db_path_));

    return stats;
}

bool Database::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB] SQL error: " << (err ? err : "unknown") << "\n";
        sqlite3_free(err);
        return false;
    }
    return true;
}

sqlite3_stmt* Database::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB] Prepare error: " << sqlite3_errmsg(db_) << "\n";
        return nullptr;
    }
    return stmt;
}

FileRecord Database::row_to_record(sqlite3_stmt* stmt) {
    FileRecord r;
    r.id         = sqlite3_column_int64(stmt, 0);
    r.name       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    r.path       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    r.parent_dir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    r.size       = sqlite3_column_int64(stmt, 4);
    r.modified   = sqlite3_column_int64(stmt, 5);
    r.is_dir     = sqlite3_column_int(stmt, 6) != 0;
    auto* ext_text = sqlite3_column_text(stmt, 7);
    r.ext = ext_text ? reinterpret_cast<const char*>(ext_text) : "";
    return r;
}

} // namespace fastsearch
