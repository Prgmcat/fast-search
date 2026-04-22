#include "core/searcher.h"
#include "core/pinyin.h"
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fastsearch {

static const std::vector<std::string> TEXT_EXTENSIONS = {
    "txt", "md", "log", "csv", "json", "xml", "yaml", "yml",
    "html", "htm", "css", "js", "ts", "jsx", "tsx",
    "c", "cpp", "h", "hpp", "cc", "cxx",
    "py", "rb", "java", "go", "rs", "swift", "kt",
    "sh", "bash", "zsh", "bat", "cmd", "ps1",
    "sql", "lua", "php", "pl", "r", "m",
    "ini", "cfg", "conf", "toml", "env",
    "makefile", "cmake", "dockerfile",
    "gitignore", "gitattributes",
};

static bool has_cjk(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = static_cast<uint8_t>(s[i]);
        if (c < 0x80) { ++i; continue; }
        uint32_t cp = 0;
        int bytes = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; bytes = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; bytes = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; bytes = 4; }
        else { ++i; continue; }
        for (int j = 1; j < bytes && (i + j) < s.size(); ++j)
            cp = (cp << 6) | (static_cast<uint8_t>(s[i + j]) & 0x3F);
        if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
        if (cp >= 0x3400 && cp <= 0x4DBF) return true;
        if (cp >= 0xF900 && cp <= 0xFAFF) return true;
        i += bytes;
    }
    return false;
}

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], sz);
    return out;
}
#endif

static std::filesystem::path make_fspath(const std::string& p) {
#ifdef _WIN32
    return std::filesystem::path(utf8_to_wide(p));
#else
    return std::filesystem::path(p);
#endif
}

Searcher::Searcher(Database& db) : db_(db) {}

std::string Searcher::make_cache_key(const SearchOptions& opts) {
    return opts.query + "|" + opts.ext_filter + "|" + opts.path_filter + "|"
         + std::to_string(opts.limit) + "|" + std::to_string(opts.offset) + "|"
         + std::to_string(opts.regex) + "|" + opts.sort_by + "|" + opts.sort_order + "|"
         + std::to_string(opts.type_filter);
}

void Searcher::invalidate_cache() { cache_.clear(); }

std::vector<FileRecord> Searcher::search_filename(const SearchOptions& opts) {
    if (opts.query.empty()) return {};

    std::string key = make_cache_key(opts);
    std::vector<FileRecord> cached;
    if (cache_.get(key, cached)) return cached;

    std::vector<FileRecord> results;
    if (opts.regex) {
        results = db_.search_regex(opts);
    } else {
        bool has_wildcards = opts.query.find('*') != std::string::npos ||
                             opts.query.find('?') != std::string::npos;
        if (has_wildcards) {
            results = db_.search_by_name(opts);
        } else if (has_cjk(opts.query)) {
            // CJK queries: use LIKE directly, FTS5 unicode61 tokenizer
            // splits each CJK character into a separate token which
            // breaks multi-character substring matching
            results = db_.search_by_name(opts);
        } else {
            results = db_.search_fts(opts);
            if (results.empty())
                results = db_.search_by_name(opts);

            if (results.size() < static_cast<size_t>(opts.limit) && looks_like_pinyin(opts.query)) {
                auto py_results = db_.search_pinyin(opts);
                for (auto& pr : py_results) {
                    bool dup = false;
                    for (auto& r : results) {
                        if (r.path == pr.path) { dup = true; break; }
                    }
                    if (!dup) {
                        results.push_back(std::move(pr));
                        if (results.size() >= static_cast<size_t>(opts.limit)) break;
                    }
                }
            }
        }
    }

    cache_.put(key, results);
    return results;
}

std::vector<ContentMatch> Searcher::search_content(const std::string& query,
                                                    const std::string& path_scope,
                                                    const std::string& ext_filter,
                                                    int limit) {
    SearchOptions fopts;
    fopts.query = "";
    fopts.path_filter = path_scope;
    fopts.ext_filter = ext_filter;
    fopts.limit = 10000;
    auto files = db_.search_by_name(fopts);

    std::vector<ContentMatch> results;
    int per_file_limit = std::max(1, limit / 10);

    for (auto& f : files) {
        if (f.is_dir) continue;
        if (!is_text_file(f.path)) continue;
        if (static_cast<int>(results.size()) >= limit) break;

        auto matches = grep_file(f.path, query, per_file_limit);
        for (auto& m : matches) {
            results.push_back(std::move(m));
            if (static_cast<int>(results.size()) >= limit) break;
        }
    }

    return results;
}

bool Searcher::is_text_file(const std::string& path) {
    namespace fs = std::filesystem;
    try {
        auto fspath = make_fspath(path);
        auto ext_u8 = fspath.extension().u8string();
        if (ext_u8.empty()) {
            auto fname_u8 = fspath.filename().u8string();
            std::string fname(fname_u8.begin(), fname_u8.end());
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            for (auto& te : TEXT_EXTENSIONS) {
                if (fname == te) return true;
            }
            return false;
        }
        std::string ext(ext_u8.begin() + 1, ext_u8.end());
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        for (auto& te : TEXT_EXTENSIONS) {
            if (ext == te) return true;
        }
    } catch (...) {}
    return false;
}

std::vector<ContentMatch> Searcher::grep_file(const std::string& path,
                                               const std::string& query,
                                               int max_matches) {
    std::vector<ContentMatch> results;
#ifdef _WIN32
    std::ifstream ifs(utf8_to_wide(path), std::ios::binary);
#else
    std::ifstream ifs(path, std::ios::binary);
#endif
    if (!ifs.is_open()) return results;

    char probe[512];
    ifs.read(probe, sizeof(probe));
    auto bytes_read = ifs.gcount();
    for (int i = 0; i < bytes_read; ++i) {
        if (probe[i] == '\0') return results;
    }
    ifs.clear();
    ifs.seekg(0);

    std::string query_lower = query;
    std::transform(query_lower.begin(), query_lower.end(), query_lower.begin(), ::tolower);

    std::string line;
    int line_num = 0;
    while (std::getline(ifs, line)) {
        ++line_num;
        std::string line_lower = line;
        std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(), ::tolower);

        if (line_lower.find(query_lower) != std::string::npos) {
            ContentMatch m;
            m.path = path;
            m.line_number = line_num;
            m.line_text = line.substr(0, 500);
            results.push_back(std::move(m));
            if (static_cast<int>(results.size()) >= max_matches) break;
        }
    }

    return results;
}

} // namespace fastsearch
