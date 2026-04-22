# FastSearch - 高性能本地文件搜索工具

类似 Everything 的高性能文件搜索工具，支持文件名搜索和文件内容搜索。

## 特性

- **极速文件名搜索** - SQLite FTS5 全文索引 + LIKE 模糊匹配
- **文件内容搜索** - 全文 grep，自动跳过二进制文件
- **实时文件监控** - 自动检测文件变更，增量更新索引
- **Web UI** - 现代暗色主题，实时搜索，浏览器访问
- **CLI** - 命令行快速搜索
- **跨平台** - Windows / Linux / macOS

## 构建

### 依赖

- CMake >= 3.16
- C++17 编译器 (MSVC / GCC / Clang)

### 编译

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Windows (Visual Studio)

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

## 使用

### 启动 Server

```bash
# 索引所有驱动器（默认）
./fastsearch-server

# 指定索引目录
./fastsearch-server D:/projects E:/data

# 指定端口
./fastsearch-server --port 8080

# 打开浏览器访问 http://127.0.0.1:9800
```

### CLI 搜索

```bash
# 文件名搜索
./fastsearch "readme.md"
./fastsearch "*.cpp" --ext cpp

# 内容搜索
./fastsearch --content "TODO" --ext py

# 查看状态
./fastsearch --status
```

## 架构

```
Client (Web UI / CLI)
    │
    │  HTTP REST API
    ▼
Server (fastsearch-server)
    ├── Indexer   → 文件系统遍历, 构建索引
    ├── Searcher  → 文件名/内容搜索引擎
    ├── Watcher   → 文件变更实时监控
    └── SQLite DB → 索引数据持久化
```

## 配置

配置文件位置:
- Windows: `%LOCALAPPDATA%/fastsearch/config.json`
- Linux/macOS: `~/.config/fastsearch/config.json`

数据库位置:
- Windows: `%LOCALAPPDATA%/fastsearch/index.db`
- Linux/macOS: `~/.local/share/fastsearch/index.db`
