#pragma once

#include "core/database.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <mutex>

namespace fastsearch {

struct ContentMatch {
    std::string path;
    int line_number;
    std::string line_text;
};

// LRU cache for search results
template<typename K, typename V>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    bool get(const K& key, V& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = map_.find(key);
        if (it == map_.end()) return false;
        list_.splice(list_.begin(), list_, it->second);
        value = it->second->second;
        return true;
    }

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            list_.erase(it->second);
            map_.erase(it);
        }
        list_.push_front({key, value});
        map_[key] = list_.begin();
        if (map_.size() > capacity_) {
            map_.erase(list_.back().first);
            list_.pop_back();
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        map_.clear();
        list_.clear();
    }

private:
    size_t capacity_;
    std::list<std::pair<K, V>> list_;
    std::unordered_map<K, typename std::list<std::pair<K, V>>::iterator> map_;
    std::mutex mtx_;
};

class Searcher {
public:
    explicit Searcher(Database& db);

    std::vector<FileRecord> search_filename(const SearchOptions& opts);
    std::vector<ContentMatch> search_content(const std::string& query,
                                             const std::string& path_scope,
                                             const std::string& ext_filter,
                                             int limit = 100);
    void invalidate_cache();

private:
    Database& db_;
    LRUCache<std::string, std::vector<FileRecord>> cache_{256};

    static std::string make_cache_key(const SearchOptions& opts);
    static bool is_text_file(const std::string& path);
    static std::vector<ContentMatch> grep_file(const std::string& path,
                                               const std::string& query,
                                               int max_matches);
};

} // namespace fastsearch
