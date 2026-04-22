#include "server/http_server.h"
#include "server/api_routes.h"

#define CPPHTTPLIB_KEEPALIVE_TIMEOUT_SECOND 10
#include "httplib.h"

#include <iostream>

namespace fastsearch {

struct HttpServer::Impl {
    httplib::Server svr;
};

HttpServer::HttpServer(Database& db, Indexer& indexer, Searcher& searcher,
                       Watcher& watcher, Config& config)
    : db_(db), indexer_(indexer), searcher_(searcher),
      watcher_(watcher), config_(config),
      impl_(std::make_unique<Impl>()) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(const std::string& host, int port) {
    register_api_routes(impl_->svr, db_, indexer_, searcher_, watcher_, config_);

    std::cout << "[Server] Starting on http://" << host << ":" << port << "\n";
    running_ = true;

    bool ok = impl_->svr.listen(host, port);
    running_ = false;
    return ok;
}

void HttpServer::stop() {
    if (running_) {
        impl_->svr.stop();
        running_ = false;
    }
}

} // namespace fastsearch
