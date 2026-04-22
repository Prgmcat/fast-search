#pragma once

#include <string>
#include <vector>
#include <functional>

namespace fastsearch {

struct FileInfo {
    std::string name;
    std::string path;
    std::string parent_dir;
    int64_t size = 0;
    int64_t modified = 0;
    bool is_dir = false;
    std::string ext;
};

enum class FileEvent { Created, Deleted, Modified, Renamed };

struct FileChange {
    FileEvent event;
    std::string path;
    std::string old_path; // for rename events
};

using FileCallback = std::function<void(const FileInfo&)>;
using WatchCallback = std::function<void(const FileChange&)>;

namespace platform {

void walk_directory(const std::string& root,
                    const std::vector<std::string>& excludes,
                    FileCallback callback);

bool start_watching(const std::string& path, WatchCallback callback);
void stop_watching();

void open_in_explorer(const std::string& path);
void open_file(const std::string& path);

std::vector<std::string> get_drive_roots();

} // namespace platform
} // namespace fastsearch
