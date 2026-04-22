#include "core/watcher.h"
#include <iostream>
#include <filesystem>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fastsearch {

Watcher::Watcher(Database& db, const Config& cfg, Searcher* searcher)
    : db_(db), config_(cfg), searcher_(searcher) {}

Watcher::~Watcher() { stop(); }

void Watcher::start(const std::vector<std::string>& paths) {
    if (running_) return;
    running_ = true;

    for (auto& p : paths) {
        platform::start_watching(p, [this](const FileChange& change) {
            handle_change(change);
        });
    }

    std::cout << "[Watcher] Monitoring " << paths.size() << " paths\n";
}

void Watcher::stop() {
    if (!running_) return;
    running_ = false;
    platform::stop_watching();
    std::cout << "[Watcher] Stopped\n";
}

static std::string to_lower_ext(const std::string& name) {
    auto pos = name.rfind('.');
    if (pos == std::string::npos || pos == name.size() - 1) return "";
    std::string ext = name.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

static std::string normalize(const std::string& p) {
    std::string out = p;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

#ifdef _WIN32
static std::wstring utf8_to_wpath(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], sz);
    return out;
}

static std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], sz, nullptr, nullptr);
    return out;
}
#endif

void Watcher::insert_or_update(const std::string& path, bool is_create) {
    namespace fs = std::filesystem;
    try {
#ifdef _WIN32
        std::wstring wp = utf8_to_wpath(path);
        fs::path fspath(wp);
#else
        fs::path fspath(path);
#endif
        if (!fs::exists(fspath)) return;

        auto stat = fs::status(fspath);
        FileRecord rec;
        rec.path = normalize(path);
#ifdef _WIN32
        rec.name = wstring_to_utf8(fspath.filename().wstring());
        rec.parent_dir = normalize(wstring_to_utf8(fspath.parent_path().wstring()));
#else
        rec.name = fspath.filename().string();
        rec.parent_dir = normalize(fspath.parent_path().string());
#endif
        rec.is_dir = fs::is_directory(stat);
        rec.ext = rec.is_dir ? "" : to_lower_ext(rec.name);
        if (!rec.is_dir) {
            rec.size = static_cast<int64_t>(fs::file_size(fspath));
        }
        auto ftime = fs::last_write_time(fspath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        rec.modified = sctp.time_since_epoch().count();

        if (is_create)
            db_.insert_file(rec);
        else
            db_.update_file(rec);
    } catch (const std::exception& e) {
        std::cerr << "[Watcher] Error processing " << path << ": " << e.what() << "\n";
    } catch (...) {}
}

void Watcher::handle_change(const FileChange& change) {
    switch (change.event) {
    case FileEvent::Created:
        insert_or_update(change.path, true);
        if (searcher_) searcher_->invalidate_cache();
        break;
    case FileEvent::Modified:
        insert_or_update(change.path, false);
        if (searcher_) searcher_->invalidate_cache();
        break;
    case FileEvent::Deleted:
        db_.delete_file(change.path);
        if (searcher_) searcher_->invalidate_cache();
        break;
    case FileEvent::Renamed:
        if (!change.old_path.empty())
            db_.delete_file(change.old_path);
        insert_or_update(change.path, true);
        if (searcher_) searcher_->invalidate_cache();
        break;
    }
}

} // namespace fastsearch
