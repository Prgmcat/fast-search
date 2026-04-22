#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <mutex>
#include <regex>

struct sqlite3;
struct sqlite3_stmt;

namespace fastsearch {

struct FileRecord {
    int64_t id = 0;
    std::string name;
    std::string path;
    std::string parent_dir;
    int64_t size = 0;
    int64_t modified = 0;
    bool is_dir = false;
    std::string ext;
    std::string name_pinyin;
};

struct SearchOptions {
    std::string query;
    std::string ext_filter;
    std::string path_filter;
    int limit = 100;
    int offset = 0;
    bool regex = false;
    bool content_search = false;
    std::string sort_by;
    std::string sort_order;
    int64_t min_size = -1;
    int64_t max_size = -1;
    int64_t min_time = -1;
    int64_t max_time = -1;
    int type_filter = -1;
};

struct IndexStats {
    int64_t total_files = 0;
    int64_t total_dirs = 0;
    int64_t db_size_bytes = 0;
};

class Database {
public:
    Database();
    ~Database();

    bool open(const std::string& db_path);
    void close();
    bool is_open() const;

    bool create_tables();
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    bool insert_file(const FileRecord& rec);
    bool insert_files_batch(const std::vector<FileRecord>& records);
    bool delete_file(const std::string& path);
    bool update_file(const FileRecord& rec);
    bool clear_all();

    std::vector<FileRecord> search_by_name(const SearchOptions& opts);
    std::vector<FileRecord> search_fts(const SearchOptions& opts);
    std::vector<FileRecord> search_regex(const SearchOptions& opts);
    std::vector<FileRecord> search_pinyin(const SearchOptions& opts);

    IndexStats get_stats();
    std::string db_path() const { return db_path_; }

private:
    sqlite3* db_ = nullptr;
    std::string db_path_;
    std::mutex mutex_;

    bool exec(const std::string& sql);
    sqlite3_stmt* prepare(const std::string& sql);
    FileRecord row_to_record(sqlite3_stmt* stmt);
};

} // namespace fastsearch
