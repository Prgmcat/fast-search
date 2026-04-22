#include "core/config.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace fastsearch {

namespace fs = std::filesystem;

std::string Config::default_db_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("LOCALAPPDATA");
    std::string base = appdata ? appdata : ".";
#else
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/.local/share" : ".";
#endif
    std::string dir = base + "/fastsearch";
    fs::create_directories(dir);
    return dir + "/index.db";
}

std::string Config::default_config_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("LOCALAPPDATA");
    std::string base = appdata ? appdata : ".";
#else
    const char* home = std::getenv("HOME");
    std::string base = home ? std::string(home) + "/.config" : ".";
#endif
    std::string dir = base + "/fastsearch";
    fs::create_directories(dir);
    return dir + "/config.json";
}

Config Config::load(const std::string& config_path) {
    Config cfg;
    cfg.db_path = default_db_path();

    std::ifstream ifs(config_path);
    if (!ifs.is_open()) return cfg;

    try {
        nlohmann::json j = nlohmann::json::parse(ifs);
        if (j.contains("db_path"))       cfg.db_path = j["db_path"];
        if (j.contains("server_host"))   cfg.server_host = j["server_host"];
        if (j.contains("server_port"))   cfg.server_port = j["server_port"];
        if (j.contains("index_paths"))   cfg.index_paths = j["index_paths"].get<std::vector<std::string>>();
        if (j.contains("exclude_patterns")) cfg.exclude_patterns = j["exclude_patterns"].get<std::vector<std::string>>();
        if (j.contains("batch_size"))    cfg.batch_size = j["batch_size"];
    } catch (const std::exception& e) {
        std::cerr << "[Config] Parse error: " << e.what() << "\n";
    }

    return cfg;
}

bool Config::save(const std::string& config_path) const {
    nlohmann::json j;
    j["db_path"] = db_path;
    j["server_host"] = server_host;
    j["server_port"] = server_port;
    j["index_paths"] = index_paths;
    j["exclude_patterns"] = exclude_patterns;
    j["batch_size"] = batch_size;

    fs::create_directories(fs::path(config_path).parent_path());
    std::ofstream ofs(config_path);
    if (!ofs.is_open()) return false;
    ofs << j.dump(2);
    return true;
}

} // namespace fastsearch
