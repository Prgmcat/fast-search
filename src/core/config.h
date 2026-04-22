#pragma once

#include <string>
#include <vector>

namespace fastsearch {

struct Config {
    std::string db_path;
    std::string server_host = "127.0.0.1";
    int server_port = 9800;
    std::vector<std::string> index_paths;
    std::vector<std::string> exclude_patterns = {
        "$Recycle.Bin", "System Volume Information",
        ".git", "node_modules", "__pycache__",
        ".svn", ".hg", "Thumbs.db", "desktop.ini"
    };
    int batch_size = 5000;

    static Config load(const std::string& config_path);
    bool save(const std::string& config_path) const;
    static std::string default_db_path();
    static std::string default_config_path();
};

} // namespace fastsearch
