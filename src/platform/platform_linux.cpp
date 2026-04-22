#if defined(__linux__)

#include "platform/platform.h"
#include <filesystem>
#include <algorithm>
#include <atomic>
#include <thread>
#include <iostream>
#include <cstring>
#include <sys/inotify.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

namespace fastsearch::platform {

static bool should_exclude(const std::string& name, const std::vector<std::string>& excludes) {
    for (auto& ex : excludes) {
        if (name == ex) return true;
    }
    return false;
}

static std::string to_lower_ext(const std::string& name) {
    auto pos = name.rfind('.');
    if (pos == std::string::npos || pos == name.size() - 1) return "";
    std::string ext = name.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

void walk_directory(const std::string& root,
                    const std::vector<std::string>& excludes,
                    FileCallback callback) {
    std::vector<std::string> stack = {root};

    while (!stack.empty()) {
        std::string current = stack.back();
        stack.pop_back();

        DIR* dir = opendir(current.c_str());
        if (!dir) continue;

        while (auto* entry = readdir(dir)) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (should_exclude(name, excludes)) continue;

            std::string full = current + "/" + name;
            struct stat st;
            if (lstat(full.c_str(), &st) != 0) continue;

            FileInfo info;
            info.name = name;
            info.path = full;
            info.parent_dir = current;
            info.size = st.st_size;
            info.modified = st.st_mtime;
            info.is_dir = S_ISDIR(st.st_mode);
            info.ext = info.is_dir ? "" : to_lower_ext(name);

            callback(info);

            if (info.is_dir) stack.push_back(full);
        }
        closedir(dir);
    }
}

static std::atomic<bool> g_watching{false};
static std::thread g_watch_thread;
static int g_inotify_fd = -1;

bool start_watching(const std::string& path, WatchCallback callback) {
    stop_watching();

    g_inotify_fd = inotify_init1(IN_NONBLOCK);
    if (g_inotify_fd < 0) return false;

    inotify_add_watch(g_inotify_fd, path.c_str(),
                      IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);

    g_watching = true;
    g_watch_thread = std::thread([callback]() {
        char buf[4096];
        while (g_watching) {
            int len = read(g_inotify_fd, buf, sizeof(buf));
            if (len <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            for (int i = 0; i < len;) {
                auto* event = reinterpret_cast<inotify_event*>(&buf[i]);
                if (event->len > 0) {
                    FileChange change;
                    change.path = event->name;
                    if (event->mask & IN_CREATE) change.event = FileEvent::Created;
                    else if (event->mask & IN_DELETE) change.event = FileEvent::Deleted;
                    else if (event->mask & IN_MODIFY) change.event = FileEvent::Modified;
                    else change.event = FileEvent::Renamed;
                    callback(change);
                }
                i += sizeof(inotify_event) + event->len;
            }
        }
    });
    return true;
}

void stop_watching() {
    g_watching = false;
    if (g_watch_thread.joinable()) g_watch_thread.join();
    if (g_inotify_fd >= 0) { close(g_inotify_fd); g_inotify_fd = -1; }
}

void open_in_explorer(const std::string& path) {
    std::string cmd = "xdg-open \"" + std::filesystem::path(path).parent_path().string() + "\"";
    system(cmd.c_str());
}

void open_file(const std::string& path) {
    std::string cmd = "xdg-open \"" + path + "\"";
    system(cmd.c_str());
}

std::vector<std::string> get_drive_roots() {
    return {"/"};
}

} // namespace fastsearch::platform

#endif // __linux__
