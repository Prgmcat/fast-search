#include "httplib.h"
#include "json.hpp"
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using json = nlohmann::json;

static std::string server_host = "127.0.0.1";
static int server_port = 9800;

#ifdef _WIN32
static std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::vector<std::string> get_utf8_args() {
    int argc_w = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    std::vector<std::string> args;
    if (argv_w) {
        for (int i = 0; i < argc_w; ++i)
            args.push_back(wstring_to_utf8(argv_w[i]));
        LocalFree(argv_w);
    }
    return args;
}
#endif

static std::string url_encode(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '*' || c == '?') {
            out << c;
        } else if (c == ' ') {
            out << '+';
        } else {
            out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << (int)c;
        }
    }
    return out.str();
}

static std::string format_size(int64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1048576) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
        return ss.str();
    }
    if (bytes < 1073741824) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytes / 1048576.0) << " MB";
        return ss.str();
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << (bytes / 1073741824.0) << " GB";
    return ss.str();
}

static void print_help() {
    std::cout << R"(FastSearch CLI - Fast File Search Tool

USAGE:
    fastsearch [OPTIONS] <query>

SEARCH:
    fastsearch <query>                   Search by filename
    fastsearch --content <query>         Search file contents
    fastsearch <query> --ext cpp         Filter by extension
    fastsearch <query> --path D:/src     Filter by path
    fastsearch <query> --limit 50        Limit results (default: 50)

MANAGEMENT:
    fastsearch --status                  Show index statistics
    fastsearch --reindex                 Rebuild the index
    fastsearch --config                  Show current config

OPTIONS:
    --host <host>     Server host (default: 127.0.0.1)
    --port <port>     Server port (default: 9800)
    --help            Show this help
)";
}

static bool api_get(const std::string& path, json& out) {
    httplib::Client cli(server_host, server_port);
    cli.set_connection_timeout(3);
    cli.set_read_timeout(30);

    auto res = cli.Get(path);
    if (!res) {
        std::cerr << "Error: Cannot connect to server at "
                  << server_host << ":" << server_port << "\n"
                  << "Make sure fastsearch-server is running.\n";
        return false;
    }
    if (res->status != 200) {
        std::cerr << "Error: Server returned status " << res->status << "\n";
        return false;
    }
    out = json::parse(res->body);
    return true;
}

static bool api_post(const std::string& path, json& out) {
    httplib::Client cli(server_host, server_port);
    cli.set_connection_timeout(3);
    auto res = cli.Post(path, "", "application/json");
    if (!res) {
        std::cerr << "Error: Cannot connect to server\n";
        return false;
    }
    out = json::parse(res->body);
    return true;
}

static void cmd_search(const std::string& query, const std::string& ext,
                       const std::string& path_filter, int limit) {
    std::string url = "/api/search?q=" + url_encode(query) + "&limit=" + std::to_string(limit);
    if (!ext.empty()) url += "&ext=" + url_encode(ext);
    if (!path_filter.empty()) url += "&path=" + url_encode(path_filter);

    json result;
    if (!api_get(url, result)) return;

    auto& items = result["results"];
    if (items.empty()) {
        std::cout << "No results found.\n";
        return;
    }

    std::cout << "Found " << result["count"].get<int>() << " result(s):\n\n";

    for (auto& item : items) {
        bool is_dir = item.value("is_dir", false);
        std::string icon = is_dir ? "[DIR]" : "     ";
        std::string name = item["name"].get<std::string>();
        std::string fpath = item["path"].get<std::string>();
        int64_t size = item.value("size", (int64_t)0);

        std::cout << "  " << icon << " " << name;
        if (!is_dir) std::cout << "  (" << format_size(size) << ")";
        std::cout << "\n        " << fpath << "\n";
    }
}

static void cmd_content_search(const std::string& query, const std::string& ext,
                                const std::string& path_filter, int limit) {
    std::string url = "/api/search/content?q=" + url_encode(query) + "&limit=" + std::to_string(limit);
    if (!ext.empty()) url += "&ext=" + url_encode(ext);
    if (!path_filter.empty()) url += "&path=" + url_encode(path_filter);

    json result;
    if (!api_get(url, result)) return;

    auto& items = result["results"];
    if (items.empty()) {
        std::cout << "No content matches found.\n";
        return;
    }

    std::cout << "Found " << result["count"].get<int>() << " match(es):\n\n";

    std::string last_path;
    for (auto& item : items) {
        std::string fpath = item["path"].get<std::string>();
        int line = item.value("line_number", 0);
        std::string text = item.value("line_text", std::string());

        if (fpath != last_path) {
            std::cout << "  " << fpath << "\n";
            last_path = fpath;
        }
        std::cout << "    L" << std::setw(5) << std::left << line << "| " << text << "\n";
    }
}

static void cmd_status() {
    json result;
    if (!api_get("/api/stats", result)) return;

    std::cout << "FastSearch Server Status\n"
              << "  Files indexed:  " << result["total_files"].get<int64_t>() << "\n"
              << "  Dirs indexed:   " << result["total_dirs"].get<int64_t>() << "\n"
              << "  Database size:  " << format_size(result["db_size_bytes"].get<int64_t>()) << "\n"
              << "  Indexing:       " << (result["indexing"].get<bool>() ? "Yes" : "No") << "\n"
              << "  Watching:       " << (result["watching"].get<bool>() ? "Yes" : "No") << "\n";
}

static void cmd_reindex() {
    std::cout << "Requesting reindex...\n";
    json result;
    if (!api_post("/api/index", result)) return;
    std::cout << "Status: " << result["status"].get<std::string>() << "\n";
}

static void cmd_config() {
    json result;
    if (!api_get("/api/config", result)) return;
    std::cout << result.dump(2) << "\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    auto args = get_utf8_args();
    int arg_count = (int)args.size();
#else
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    int arg_count = argc;
#endif

    if (arg_count < 2) { print_help(); return 0; }

    std::string query;
    std::string ext;
    std::string path_filter;
    int limit = 50;
    bool content_mode = false;
    bool status_mode = false;
    bool reindex_mode = false;
    bool config_mode = false;

    for (int i = 1; i < arg_count; ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") { print_help(); return 0; }
        else if (arg == "--content" || arg == "-c") { content_mode = true; }
        else if (arg == "--status" || arg == "-s") { status_mode = true; }
        else if (arg == "--reindex") { reindex_mode = true; }
        else if (arg == "--config") { config_mode = true; }
        else if (arg == "--ext" && i + 1 < arg_count) { ext = args[++i]; }
        else if (arg == "--path" && i + 1 < arg_count) { path_filter = args[++i]; }
        else if (arg == "--limit" && i + 1 < arg_count) { limit = std::stoi(args[++i]); }
        else if (arg == "--host" && i + 1 < arg_count) { server_host = args[++i]; }
        else if (arg == "--port" && i + 1 < arg_count) { server_port = std::stoi(args[++i]); }
        else if (arg[0] != '-') { query = arg; }
    }

    if (status_mode) { cmd_status(); return 0; }
    if (reindex_mode) { cmd_reindex(); return 0; }
    if (config_mode) { cmd_config(); return 0; }

    if (query.empty()) {
        std::cerr << "Error: No search query provided.\n";
        print_help();
        return 1;
    }

    if (content_mode)
        cmd_content_search(query, ext, path_filter, limit);
    else
        cmd_search(query, ext, path_filter, limit);

    return 0;
}
