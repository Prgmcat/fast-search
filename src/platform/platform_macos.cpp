#if defined(__APPLE__)

#include "platform/platform.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <dirent.h>
#include <sys/stat.h>
#include <CoreServices/CoreServices.h>
#include <atomic>
#include <thread>

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
static FSEventStreamRef g_stream = nullptr;
static WatchCallback g_callback;

static void fsevents_callback(ConstFSEventStreamRef,
                              void*,
                              size_t numEvents,
                              void* eventPaths,
                              const FSEventStreamEventFlags eventFlags[],
                              const FSEventStreamEventId[]) {
    auto** paths = static_cast<char**>(eventPaths);
    for (size_t i = 0; i < numEvents; ++i) {
        FileChange change;
        change.path = paths[i];
        if (eventFlags[i] & kFSEventStreamEventFlagItemCreated)
            change.event = FileEvent::Created;
        else if (eventFlags[i] & kFSEventStreamEventFlagItemRemoved)
            change.event = FileEvent::Deleted;
        else if (eventFlags[i] & kFSEventStreamEventFlagItemRenamed)
            change.event = FileEvent::Renamed;
        else
            change.event = FileEvent::Modified;
        if (g_callback) g_callback(change);
    }
}

bool start_watching(const std::string& path, WatchCallback callback) {
    stop_watching();
    g_callback = callback;

    CFStringRef cfpath = CFStringCreateWithCString(nullptr, path.c_str(), kCFStringEncodingUTF8);
    CFArrayRef paths = CFArrayCreate(nullptr, (const void**)&cfpath, 1, &kCFTypeArrayCallBacks);

    FSEventStreamContext ctx = {0, nullptr, nullptr, nullptr, nullptr};
    g_stream = FSEventStreamCreate(nullptr, fsevents_callback, &ctx,
                                   paths, kFSEventStreamEventIdSinceNow,
                                   0.5, kFSEventStreamCreateFlagFileEvents);

    FSEventStreamScheduleWithRunLoop(g_stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    FSEventStreamStart(g_stream);
    g_watching = true;

    CFRelease(paths);
    CFRelease(cfpath);
    return true;
}

void stop_watching() {
    if (g_stream) {
        FSEventStreamStop(g_stream);
        FSEventStreamInvalidate(g_stream);
        FSEventStreamRelease(g_stream);
        g_stream = nullptr;
    }
    g_watching = false;
}

void open_in_explorer(const std::string& path) {
    std::string dir = std::filesystem::path(path).parent_path().string();
    std::string cmd = "open \"" + dir + "\"";
    system(cmd.c_str());
}

void open_file(const std::string& path) {
    std::string cmd = "open \"" + path + "\"";
    system(cmd.c_str());
}

std::vector<std::string> get_drive_roots() {
    return {"/"};
}

} // namespace fastsearch::platform

#endif // __APPLE__
