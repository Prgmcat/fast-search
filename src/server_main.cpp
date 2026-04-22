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
#include <atomic>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static fastsearch::HttpServer* g_server = nullptr;
static std::atomic<bool> g_stop_requested{false};

#ifdef _WIN32
static const wchar_t* SERVICE_NAME = L"FastSearchService";
static SERVICE_STATUS_HANDLE g_svc_handle = nullptr;
static SERVICE_STATUS g_svc_status = {};

static std::string w2u(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}
#endif

static void log_msg(const std::string& msg) {
    std::cout << msg << std::flush;
}

// ── Core server logic (shared by console & service modes) ──

static int run_server(const std::vector<std::string>& extra_paths) {
    using namespace fastsearch;

    auto config_path = Config::default_config_path();
    Config config = Config::load(config_path);

    for (auto& p : extra_paths)
        config.index_paths.push_back(p);

    if (config.index_paths.empty())
        config.index_paths = platform::get_drive_roots();

    config.save(config_path);

    Database db;
    if (!db.open(config.db_path)) {
        log_msg("[Error] Cannot open database: " + config.db_path + "\n");
        return 1;
    }
    db.create_tables();

    Indexer indexer(db, config);
    Searcher searcher(db);
    Watcher watcher(db, config, &searcher);

    auto stats = db.get_stats();
    watcher.start(config.index_paths);

    indexer.set_progress_callback([](int64_t n, const std::string&) {
        log_msg("\r[Indexer] " + std::to_string(n) + " entries indexed...   ");
    });

    if (stats.total_files == 0 && stats.total_dirs == 0)
        log_msg("[Server] Empty database, starting initial index...\n");
    else
        log_msg("[Server] Database has " + std::to_string(stats.total_files) +
                " files, " + std::to_string(stats.total_dirs) + " dirs. Syncing...\n");

    std::thread([&]() {
        indexer.build_index(config.index_paths);
        log_msg("\n[Indexer] Sync complete.\n");
    }).detach();

    HttpServer server(db, indexer, searcher, watcher, config);
    g_server = &server;

    log_msg("[Server] Open http://" + config.server_host + ":" +
            std::to_string(config.server_port) + "\n");

    server.start(config.server_host, config.server_port);

    watcher.stop();
    db.close();
    g_server = nullptr;
    return 0;
}

// ── Console mode ──

static void signal_handler(int) {
    log_msg("\n[Server] Shutting down...\n");
    g_stop_requested = true;
    if (g_server) g_server->stop();
}

static int run_console(const std::vector<std::string>& args, int arg_count) {
    using namespace fastsearch;

    auto config_path = Config::default_config_path();
    Config config = Config::load(config_path);
    std::vector<std::string> extra_paths;

    for (int i = 1; i < arg_count; ++i) {
        const std::string& arg = args[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < arg_count)
            config.server_port = std::stoi(args[++i]);
        else if (arg == "--host" && i + 1 < arg_count)
            config.server_host = args[++i];
        else if (arg == "--db" && i + 1 < arg_count)
            config.db_path = args[++i];
        else if (!arg.empty() && arg[0] != '-')
            extra_paths.push_back(arg);
    }
    config.save(config_path);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    return run_server(extra_paths);
}

// ── Windows Service ──

#ifdef _WIN32

static void report_svc_status(DWORD state, DWORD exit_code, DWORD wait_hint) {
    static DWORD checkpoint = 1;
    g_svc_status.dwCurrentState = state;
    g_svc_status.dwWin32ExitCode = exit_code;
    g_svc_status.dwWaitHint = wait_hint;
    g_svc_status.dwControlsAccepted =
        (state == SERVICE_START_PENDING) ? 0 : SERVICE_ACCEPT_STOP;
    g_svc_status.dwCheckPoint =
        (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkpoint++;
    SetServiceStatus(g_svc_handle, &g_svc_status);
}

static DWORD WINAPI svc_ctrl_handler(DWORD ctrl, DWORD, LPVOID, LPVOID) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
        report_svc_status(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        g_stop_requested = true;
        if (g_server) g_server->stop();
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static void WINAPI service_main(DWORD, LPWSTR*) {
    g_svc_handle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, svc_ctrl_handler, nullptr);
    if (!g_svc_handle) return;

    g_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    report_svc_status(SERVICE_START_PENDING, NO_ERROR, 3000);

    auto cfg_path = fastsearch::Config::default_config_path();
    std::string log_path = cfg_path;
    auto last_sep = log_path.rfind('/');
    if (last_sep == std::string::npos) last_sep = log_path.rfind('\\');
    if (last_sep != std::string::npos) log_path = log_path.substr(0, last_sep + 1);
    log_path += "fastsearch-service.log";
    std::ofstream log_file(log_path, std::ios::app);
    if (log_file.is_open()) {
        std::cout.rdbuf(log_file.rdbuf());
        std::cerr.rdbuf(log_file.rdbuf());
    }

    report_svc_status(SERVICE_RUNNING, NO_ERROR, 0);
    log_msg("[Service] FastSearch service started.\n");

    int rc = run_server({});

    log_msg("[Service] FastSearch service stopped.\n");
    report_svc_status(SERVICE_STOPPED, rc ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR, 0);
}

static bool install_service(const std::string& exe_path) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) {
        std::cerr << "[Error] Cannot open SCM. Run as Administrator.\n";
        return false;
    }

    std::wstring wpath(exe_path.begin(), exe_path.end());
    wpath = L"\"" + wpath + L"\" --service";

    SC_HANDLE svc = CreateServiceW(
        scm, SERVICE_NAME, L"FastSearch File Search Service",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        wpath.c_str(),
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!svc) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS)
            std::cerr << "[Error] Service already exists. Use --uninstall first.\n";
        else
            std::cerr << "[Error] CreateService failed: " << err << "\n";
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_DESCRIPTIONW desc;
    wchar_t desc_text[] = L"FastSearch local file search service with Web UI";
    desc.lpDescription = desc_text;
    ChangeServiceConfig2W(svc, SERVICE_CONFIG_DESCRIPTION, &desc);

    std::cout << "[OK] Service installed successfully.\n";
    std::cout << "     Start:  sc start FastSearchService\n";
    std::cout << "     Stop:   sc stop  FastSearchService\n";
    std::cout << "     Remove: fastsearch-server --uninstall\n";

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

static bool uninstall_service() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        std::cerr << "[Error] Cannot open SCM. Run as Administrator.\n";
        return false;
    }

    SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
    if (!svc) {
        std::cerr << "[Error] Service not found.\n";
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS status;
    if (ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        std::cout << "[Info] Stopping service...\n";
        int retries = 30;
        while (retries-- > 0) {
            Sleep(1000);
            if (QueryServiceStatus(svc, &status) && status.dwCurrentState == SERVICE_STOPPED)
                break;
        }
    }

    if (!DeleteService(svc)) {
        std::cerr << "[Error] DeleteService failed: " << GetLastError() << "\n";
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return false;
    }

    std::cout << "[OK] Service uninstalled successfully.\n";
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

#endif // _WIN32

// ── main ──

int main(int argc, char* argv[]) {
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

    for (int i = 1; i < arg_count; ++i) {
        const std::string& arg = args[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "FastSearch Server\n"
                      << "Usage: fastsearch-server [options] [paths...]\n\n"
                      << "Options:\n"
                      << "  --host       Host to bind (default: 127.0.0.1)\n"
                      << "  --port, -p   Port to bind (default: 9800)\n"
                      << "  --db         Database path\n"
#ifdef _WIN32
                      << "  --install    Install as Windows service\n"
                      << "  --uninstall  Uninstall Windows service\n"
                      << "  --service    (internal) Run as Windows service\n"
#endif
                      << "  --help       Show this help\n";
            return 0;
        }
#ifdef _WIN32
        if (arg == "--install") {
            wchar_t exe[MAX_PATH];
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            return install_service(w2u(exe)) ? 0 : 1;
        }
        if (arg == "--uninstall") {
            return uninstall_service() ? 0 : 1;
        }
        if (arg == "--service") {
            SERVICE_TABLE_ENTRYW table[] = {
                { const_cast<LPWSTR>(SERVICE_NAME), service_main },
                { nullptr, nullptr }
            };
            if (!StartServiceCtrlDispatcherW(table)) {
                std::cerr << "[Error] StartServiceCtrlDispatcher failed: "
                          << GetLastError() << "\n";
                return 1;
            }
            return 0;
        }
#endif
    }

    return run_console(args, arg_count);
}
