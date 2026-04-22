#pragma once

#include "core/database.h"
#include "core/config.h"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace fastsearch {

class Indexer {
public:
    using ProgressCallback = std::function<void(int64_t indexed, const std::string& current_path)>;

    explicit Indexer(Database& db, const Config& cfg);

    void build_index(const std::vector<std::string>& paths);
    void rebuild_index();
    void stop();

    bool is_running() const { return running_; }
    int64_t files_indexed() const { return files_indexed_; }

    void set_progress_callback(ProgressCallback cb) { progress_cb_ = std::move(cb); }

private:
    Database& db_;
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> files_indexed_{0};
    ProgressCallback progress_cb_;

    void index_directory(const std::string& path);
};

} // namespace fastsearch
