FastSearch — 本地文件极速搜索

简介
────
FastSearch 是一款本地文件名闪电搜索工具：
  • 本地索引（SQLite + FTS5），毫秒级模糊/通配/正则/拼音搜索
  • 实时文件系统监控，增删改即时反映
  • 嵌入式 WebView2 图形界面，Windows 原生右键菜单
  • 可选注册为 Windows 服务，开机自启后台常驻

系统要求
────────
  • Windows 10 / 11 (64 位)
  • WebView2 Runtime（Win10 1803+ 已内置；如缺失请至
    https://developer.microsoft.com/microsoft-edge/webview2/ 下载）

组件说明
────────
本安装程序会部署以下文件到安装目录（默认 %ProgramFiles%\FastSearch）：
  • fastsearch-gui.exe     — 图形界面（基于 WebView2）
  • fastsearch-server.exe  — 后端服务器（REST API + 文件索引）
  • fastsearch.exe         — 命令行客户端
  • WebView2Loader.dll     — WebView2 加载器
  • fastsearch.ico         — 应用图标

如在安装向导中勾选"作为 Windows 服务安装"，安装程序会：
  1) 注册名为 "FastSearchService" 的服务；
  2) 立即启动该服务（监听 127.0.0.1:9800）。

卸载时会自动停止并移除该服务，并清理 WebView2 用户数据目录。
本地索引数据库与日志默认保留，如需完全清理请手动删除
%USERPROFILE%\.fastsearch\ 与 %PROGRAMDATA%\FastSearch\ 目录。

───────────────────────────────────────────────────────
English summary

FastSearch is a blazing-fast local file-name search utility powered by
SQLite FTS5 and real-time filesystem watching, with a WebView2-based GUI,
a REST HTTP server and a CLI. Windows 10/11 x64 + WebView2 Runtime are
required. The wizard can optionally register "FastSearchService" so the
backend starts automatically at boot. Uninstallation removes the service;
your index database and logs are left in place unless removed manually.
