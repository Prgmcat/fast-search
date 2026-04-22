#include "core/indexer.h"
#include "platform/platform.h"
#include <iostream>
#include <chrono>

namespace fastsearch {

Indexer::Indexer(Database& db, const Config& cfg)
    : db_(db), config_(cfg) {}

void Indexer::build_index(const std::vector<std::string>& paths) {
    running_ = true;
    files_indexed_ = 0;

    auto t0 = std::chrono::steady_clock::now();

    for (auto& p : paths) {
        if (!running_) break;
        index_directory(p);
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "[Indexer] Done: " << files_indexed_ << " entries in " << ms << " ms\n";

    running_ = false;
}

void Indexer::rebuild_index() {
    db_.clear_all();
    build_index(config_.index_paths);
}

void Indexer::stop() {
    running_ = false;
}

void Indexer::index_directory(const std::string& path) {
    std::vector<FileRecord> batch;
    batch.reserve(config_.batch_size);

    db_.begin_transaction();

    platform::walk_directory(path, config_.exclude_patterns,
        [&](const FileInfo& info) {
            if (!running_) return;

            FileRecord rec;
            rec.name = info.name;
            rec.path = info.path;
            rec.parent_dir = info.parent_dir;
            rec.size = info.size;
            rec.modified = info.modified;
            rec.is_dir = info.is_dir;
            rec.ext = info.ext;
            batch.push_back(std::move(rec));

            if (static_cast<int>(batch.size()) >= config_.batch_size) {
                db_.insert_files_batch(batch);
                files_indexed_ += batch.size();
                if (progress_cb_)
                    progress_cb_(files_indexed_, info.path);
                batch.clear();
                db_.commit_transaction();
                db_.begin_transaction();
            }
        });

    if (!batch.empty()) {
        db_.insert_files_batch(batch);
        files_indexed_ += batch.size();
        if (progress_cb_ && !batch.empty())
            progress_cb_(files_indexed_, batch.back().path);
    }

    db_.commit_transaction();
}

} // namespace fastsearch
