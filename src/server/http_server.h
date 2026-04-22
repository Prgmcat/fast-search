#pragma once

#include "core/database.h"
#include "core/indexer.h"
#include "core/searcher.h"
#include "core/watcher.h"
#include "core/config.h"
#include <string>
#include <memory>
#include <thread>

namespace fastsearch {

class HttpServer {
public:
    HttpServer(Database& db, Indexer& indexer, Searcher& searcher,
               Watcher& watcher, Config& config);
    ~HttpServer();

    bool start(const std::string& host, int port);
    void stop();
    bool is_running() const { return running_; }

private:
    Database& db_;
    Indexer& indexer_;
    Searcher& searcher_;
    Watcher& watcher_;
    Config& config_;
    bool running_ = false;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fastsearch
