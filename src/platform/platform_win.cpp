#ifdef _WIN32

#include "platform/platform.h"
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <iostream>
#include <filesystem>

namespace fastsearch::platform {

static std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(sz, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &out[0], sz, nullptr, nullptr);
    return out;
}

static std::wstring utf8_to_wstring(const std::string& s) {
    if (s.empty()) return {};
    int sz = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(sz, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], sz);
    return out;
}

static bool should_exclude(const std::string& name, const std::vector<std::string>& excludes) {
    for (auto& ex : excludes) {
        if (_stricmp(name.c_str(), ex.c_str()) == 0) return true;
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

static std::string normalize_path(const std::string& p) {
    std::string out = p;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

void walk_directory(const std::string& root,
                    const std::vector<std::string>& excludes,
                    FileCallback callback) {

    struct DirEntry { std::wstring path; };
    std::vector<DirEntry> stack;
    stack.push_back({utf8_to_wstring(root)});

    while (!stack.empty()) {
        auto current = stack.back();
        stack.pop_back();

        std::wstring pattern = current.path + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            std::wstring wname(fd.cFileName);
            if (wname == L"." || wname == L"..") continue;

            std::string name = wstring_to_utf8(wname);
            if (should_exclude(name, excludes)) continue;

            std::wstring full = current.path + L"\\" + wname;
            std::string full_utf8 = normalize_path(wstring_to_utf8(full));
            std::string parent = normalize_path(wstring_to_utf8(current.path));

            bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

            ULARGE_INTEGER filesize;
            filesize.LowPart = fd.nFileSizeLow;
            filesize.HighPart = fd.nFileSizeHigh;

            ULARGE_INTEGER ftime;
            ftime.LowPart = fd.ftLastWriteTime.dwLowDateTime;
            ftime.HighPart = fd.ftLastWriteTime.dwHighDateTime;
            int64_t unix_time = static_cast<int64_t>((ftime.QuadPart - 116444736000000000ULL) / 10000000ULL);

            FileInfo info;
            info.name = name;
            info.path = full_utf8;
            info.parent_dir = parent;
            info.size = static_cast<int64_t>(filesize.QuadPart);
            info.modified = unix_time;
            info.is_dir = is_dir;
            info.ext = is_dir ? "" : to_lower_ext(name);

            callback(info);

            if (is_dir) {
                stack.push_back({full});
            }
        } while (FindNextFileW(hFind, &fd));

        FindClose(hFind);
    }
}

// ── Multi-path file watching ──

struct WatchContext {
    HANDLE hDir = INVALID_HANDLE_VALUE;
    HANDLE hStopEvent = nullptr;
    std::thread thread;
    std::string root_path;
};

static std::atomic<bool> g_watching{false};
static std::vector<WatchContext*> g_watchers;
static std::mutex g_watchers_mutex;

bool start_watching(const std::string& path, WatchCallback callback) {
    std::wstring wpath = utf8_to_wstring(path);
    HANDLE hDir = CreateFileW(
        wpath.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (hDir == INVALID_HANDLE_VALUE) {
        std::cerr << "[Watch] Cannot open directory: " << path << "\n";
        return false;
    }

    auto* ctx = new WatchContext();
    ctx->hDir = hDir;
    ctx->hStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ctx->root_path = path;

    g_watching = true;

    ctx->thread = std::thread([ctx, callback]() {
        alignas(DWORD) char buffer[65536];
        OVERLAPPED overlapped = {};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        HANDLE events[2] = { overlapped.hEvent, ctx->hStopEvent };

        while (g_watching) {
            ResetEvent(overlapped.hEvent);

            BOOL ok = ReadDirectoryChangesW(
                ctx->hDir, buffer, sizeof(buffer), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_CREATION,
                nullptr, &overlapped, nullptr);

            if (!ok) break;

            DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait != WAIT_OBJECT_0) break;

            DWORD bytes = 0;
            if (!GetOverlappedResult(ctx->hDir, &overlapped, &bytes, FALSE) || bytes == 0)
                continue;

            auto* fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
            while (true) {
                std::wstring wname(fni->FileName, fni->FileNameLength / sizeof(WCHAR));
                std::string fullpath = normalize_path(ctx->root_path + "/" + wstring_to_utf8(wname));

                FileChange change;
                change.path = fullpath;
                switch (fni->Action) {
                    case FILE_ACTION_ADDED:
                        change.event = FileEvent::Created; break;
                    case FILE_ACTION_REMOVED:
                        change.event = FileEvent::Deleted; break;
                    case FILE_ACTION_MODIFIED:
                        change.event = FileEvent::Modified; break;
                    case FILE_ACTION_RENAMED_OLD_NAME:
                        change.event = FileEvent::Renamed;
                        change.old_path = fullpath;
                        break;
                    case FILE_ACTION_RENAMED_NEW_NAME:
                        change.event = FileEvent::Created;
                        break;
                    default: break;
                }
                try { callback(change); } catch (...) {}

                if (fni->NextEntryOffset == 0) break;
                fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                    reinterpret_cast<char*>(fni) + fni->NextEntryOffset);
            }
        }

        CloseHandle(overlapped.hEvent);
        CloseHandle(ctx->hDir);
    });

    std::lock_guard<std::mutex> lock(g_watchers_mutex);
    g_watchers.push_back(ctx);

    std::cout << "[Watch] Monitoring: " << path << "\n";
    return true;
}

void stop_watching() {
    if (!g_watching) return;
    g_watching = false;

    std::lock_guard<std::mutex> lock(g_watchers_mutex);
    for (auto* ctx : g_watchers) {
        if (ctx->hStopEvent) SetEvent(ctx->hStopEvent);
    }
    for (auto* ctx : g_watchers) {
        if (ctx->thread.joinable()) ctx->thread.join();
        if (ctx->hStopEvent) CloseHandle(ctx->hStopEvent);
        delete ctx;
    }
    g_watchers.clear();
}

void open_in_explorer(const std::string& path) {
    std::wstring wp = utf8_to_wstring(path);
    std::replace(wp.begin(), wp.end(), L'/', L'\\');
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"/select,\"" + wp + L"\"").c_str(), nullptr, SW_SHOW);
}

void open_file(const std::string& path) {
    std::wstring wp = utf8_to_wstring(path);
    ShellExecuteW(nullptr, L"open", wp.c_str(), nullptr, nullptr, SW_SHOW);
}

std::vector<std::string> get_drive_roots() {
    std::vector<std::string> roots;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            char letter = 'A' + i;
            roots.push_back(std::string(1, letter) + ":");
        }
    }
    return roots;
}

} // namespace fastsearch::platform

#endif // _WIN32
