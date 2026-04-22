#include "core/database.h"
#include "core/config.h"
#include "core/indexer.h"
#include "core/searcher.h"
#include "core/watcher.h"
#include "server/http_server.h"
#include "platform/platform.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static fastsearch::HttpServer* g_server = nullptr;

static void signal_handler(int) {
    std::cout << "\n[Server] Shutting down...\n";
    if (g_server) g_server->stop();
}

#ifdef _WIN32
static std::string w2u(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}
#endif

int main(int argc, char* argv[]) {
    using namespace fastsearch;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    int argc_w = 0;
    LPWSTR* argv_w = CommandLineToArgvW(GetCommandLineW(), &argc_w);
    std::vector<std::string> args;
    for (int i = 0; i < argc_w; ++i) args.push_back(w2u(argv_w[i]));
    LocalFree(argv_w);
    int arg_count = (int)args.size();
#else
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    int arg_count = argc;
#endif

    auto config_path = Config::default_config_path();
    Config config = Config::load(config_path);

    for (int i = 1; i < arg_count; ++i) {
        const std::string& arg = args[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < arg_count)
            config.server_port = std::stoi(args[++i]);
        else if ((arg == "--host") && i + 1 < arg_count)
            config.server_host = args[++i];
        else if ((arg == "--db") && i + 1 < arg_count)
            config.db_path = args[++i];
        else if (arg == "--help") {
            std::cout << "FastSearch Server\n"
                      << "Usage: fastsearch-server [options] [paths...]\n"
                      << "  --host      Host to bind (default: 127.0.0.1)\n"
                      << "  --port, -p  Port to bind (default: 9800)\n"
                      << "  --db        Database path\n"
                      << "  --help      Show this help\n";
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            config.index_paths.push_back(arg);
        }
    }

    if (config.index_paths.empty())
        config.index_paths = platform::get_drive_roots();

    config.save(config_path);

    Database db;
    if (!db.open(config.db_path)) {
        std::cerr << "[Error] Cannot open database: " << config.db_path << "\n";
        return 1;
    }
    db.create_tables();

    Indexer indexer(db, config);
    Searcher searcher(db);
    Watcher watcher(db, config, &searcher);

    auto stats = db.get_stats();

    watcher.start(config.index_paths);

    indexer.set_progress_callback([](int64_t n, const std::string&) {
        std::cout << "\r[Indexer] " << n << " entries indexed...   " << std::flush;
    });

    if (stats.total_files == 0 && stats.total_dirs == 0) {
        std::cout << "[Server] Empty database, starting initial index...\n";
    } else {
        std::cout << "[Server] Database has " << stats.total_files << " files, "
                  << stats.total_dirs << " dirs. Syncing...\n";
    }

    std::cout << "[Server] Indexing: ";
    for (auto& p : config.index_paths) std::cout << p << " ";
    std::cout << "\n";

    std::thread([&]() {
        indexer.build_index(config.index_paths);
        std::cout << "\n[Indexer] Sync complete.\n";
    }).detach();

    HttpServer server(db, indexer, searcher, watcher, config);
    g_server = &server;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[Server] Open http://" << config.server_host << ":"
              << config.server_port << " in your browser\n";

    server.start(config.server_host, config.server_port);

    watcher.stop();
    db.close();
    return 0;
}
