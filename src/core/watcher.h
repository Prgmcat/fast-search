#pragma once

#include "core/database.h"
#include "core/config.h"
#include "core/searcher.h"
#include "platform/platform.h"
#include <thread>
#include <atomic>

namespace fastsearch {

class Watcher {
public:
    Watcher(Database& db, const Config& cfg, Searcher* searcher = nullptr);
    ~Watcher();

    void start(const std::vector<std::string>& paths);
    void stop();
    bool is_running() const { return running_; }

private:
    Database& db_;
    Config config_;
    Searcher* searcher_;
    std::atomic<bool> running_{false};

    void handle_change(const FileChange& change);
    void insert_or_update(const std::string& path, bool is_create);
};

} // namespace fastsearch
