#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <functional>

#include "WebView2.h"
#include <wrl.h>

#include "resource.h"

using namespace Microsoft::WRL;

static const wchar_t* WINDOW_CLASS = L"FastSearchGUI";
static const wchar_t* WINDOW_TITLE = L"FastSearch";
static const int DEFAULT_WIDTH = 1100;
static const int DEFAULT_HEIGHT = 750;

// Single instance: a second process posts this to the first instance's HWND.
static const wchar_t* SINGLETON_MSG_NAME = L"FastSearch_GUISingletonActivate_v1";
static const wchar_t* SINGLETON_MUTEX_NAME = L"Local\\FastSearch-9E0C4A1B-3F8D-4E2A-9B7C-GUISingleton";
static UINT g_wm_singleton_activate = 0;

static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static HWND g_hwnd = nullptr;
static std::wstring g_url = L"http://127.0.0.1:9800";

// ── Tray / hide-to-tray state ────────────────────────────────
// Closing the main window hides it to the notification area; the process
// only truly exits when the user picks "退出程序" from the tray menu.
constexpr UINT WM_TRAYICON       = WM_APP + 1;
constexpr UINT IDM_TRAY_SHOW     = 0x9001;
constexpr UINT IDM_TRAY_EXIT     = 0x9002;
constexpr UINT TRAY_UID          = 1;

static NOTIFYICONDATAW g_nid = {};
static bool g_tray_installed = false;
static bool g_really_exit    = false;
static bool g_first_hide     = true;

// Live IContextMenu2/3 pointers while a shell popup menu is being tracked.
// Set right before TrackPopupMenuEx, cleared immediately after. The window
// procedure forwards menu-related messages to these while they are alive so
// that submenus and owner-draw entries from shell extensions render correctly.
static ComPtr<IContextMenu2> g_cm2;
static ComPtr<IContextMenu3> g_cm3;

static void show_shell_context_menu(HWND hwnd, const std::wstring& path_in,
                                    int client_x, int client_y, bool shift);
static bool rename_item(HWND owner, const std::wstring& path,
                        const std::wstring& new_name);

static const wchar_t* OFFLINE_HTML = LR"HTML(
<html><head><meta charset="UTF-8"><style>
body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;
font-family:'Segoe UI',system-ui,sans-serif;background:#0f1117;color:#e0e0e8}
.box{text-align:center;padding:40px}
h2{font-size:1.6rem;margin-bottom:12px;color:#f87171}
p{color:#8888a0;margin-bottom:24px;font-size:.95rem}
button{padding:12px 32px;border:none;border-radius:10px;
background:linear-gradient(135deg,#5b7ff5,#8b5cf6);color:#fff;
font-size:1rem;cursor:pointer;font-weight:600}
button:hover{opacity:.9}
</style></head><body><div class="box">
<h2>FastSearch Server Offline</h2>
<p>Cannot connect to the server. Please make sure fastsearch-server is running.</p>
<button onclick="location.reload()">Retry</button>
</div></body></html>)HTML";

static void resize_webview(HWND hwnd) {
    if (!g_controller) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    g_controller->put_Bounds(rc);
}

// WebView2 must write profile/cache to a *user-writable* folder. When
// `userDataFolder` is null, the runtime defaults to a subfolder next to
// the executable (e.g. `fastsearch-gui.exe.WebView2`). That path lives under
// `Program Files` when installed, where normal users cannot create files —
// `CreateCoreWebView2Environment` then fails and the view stays black.
// Keep data under %LocalAppData%\FastSearch\WebView2Data (same as dev runs).
static std::wstring s_webview_user_data;
static const wchar_t* make_webview_user_data_path() {
    WCHAR base[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, base)))
        return nullptr;
    std::wstring app_root = std::wstring(base) + L"\\FastSearch";
    if (!CreateDirectoryW(app_root.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) return nullptr;
    }
    s_webview_user_data = app_root + L"\\WebView2Data";
    if (!CreateDirectoryW(s_webview_user_data.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS) return nullptr;
    }
    return s_webview_user_data.c_str();
}

static void init_webview(HWND hwnd) {
    const wchar_t* user_data = make_webview_user_data_path();
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(hr) || !env) return hr;
                env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                            if (FAILED(hr) || !ctrl) return hr;
                            g_controller = ctrl;
                            g_controller->get_CoreWebView2(&g_webview);

                            ICoreWebView2Settings* settings = nullptr;
                            g_webview->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(TRUE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            g_webview->add_NavigationCompleted(
                                Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                        BOOL success = FALSE;
                                        args->get_IsSuccess(&success);
                                        if (!success) {
                                            sender->NavigateToString(OFFLINE_HTML);
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);

                            // Receive right-click requests from the web UI.
                            // Message format: ctxmenu|<x>|<y>|<shift>|<path>
                            // Split on the first 4 '|' so the path (last field)
                            // may contain '|' if Windows ever allows it.
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [hwnd](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw)
                                            return S_OK;
                                        std::wstring msg(raw);
                                        CoTaskMemFree(raw);

                                        // doRename|<old_path>|<new_name>
                                        // Perform the actual file rename and notify JS.
                                        const std::wstring do_rename = L"doRename|";
                                        if (msg.rfind(do_rename, 0) == 0) {
                                            size_t sep = msg.find(L'|', do_rename.size());
                                            if (sep == std::wstring::npos) return S_OK;
                                            std::wstring old_path =
                                                msg.substr(do_rename.size(),
                                                           sep - do_rename.size());
                                            std::wstring new_name = msg.substr(sep + 1);
                                            bool ok = rename_item(hwnd, old_path, new_name);
                                            std::wstring new_path;
                                            if (ok) {
                                                size_t slash = old_path.find_last_of(L'\\');
                                                new_path = (slash != std::wstring::npos)
                                                    ? old_path.substr(0, slash + 1) + new_name
                                                    : new_name;
                                            }
                                            std::wstring reply = L"renameResult|";
                                            reply += (ok ? L"1|" : L"0|");
                                            reply += old_path;
                                            reply += L"|";
                                            reply += new_path;
                                            g_webview->PostWebMessageAsString(reply.c_str());
                                            return S_OK;
                                        }

                                        const std::wstring prefix = L"ctxmenu|";
                                        if (msg.rfind(prefix, 0) != 0) return S_OK;

                                        size_t p = prefix.size();
                                        auto next = [&](wchar_t sep) -> std::wstring {
                                            size_t q = msg.find(sep, p);
                                            if (q == std::wstring::npos) {
                                                std::wstring r = msg.substr(p);
                                                p = msg.size();
                                                return r;
                                            }
                                            std::wstring r = msg.substr(p, q - p);
                                            p = q + 1;
                                            return r;
                                        };

                                        std::wstring sx = next(L'|');
                                        std::wstring sy = next(L'|');
                                        std::wstring ss = next(L'|');
                                        std::wstring path = msg.substr(p);

                                        int x = _wtoi(sx.c_str());
                                        int y = _wtoi(sy.c_str());
                                        bool shift = !ss.empty() && ss[0] == L'1';

                                        show_shell_context_menu(hwnd, path, x, y, shift);
                                        return S_OK;
                                    }).Get(), nullptr);

                            resize_webview(hwnd);
                            g_webview->Navigate(g_url.c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

// Replace forward slashes with backslashes so SHParseDisplayName accepts the
// path (the shell API prefers native separators).
static std::wstring normalize_path(std::wstring s) {
    for (auto& c : s) if (c == L'/') c = L'\\';
    return s;
}

// Custom menu item IDs, chosen to sit above the range [1, 0x7FFF] that we
// hand out to IContextMenu::QueryContextMenu so the two never collide.
constexpr UINT IDM_CUSTOM_FIRST     = 0xF001;
constexpr UINT IDM_OPEN_LOCATION    = 0xF001;
constexpr UINT IDM_COPY_FULL_PATH   = 0xF002;
constexpr UINT IDM_CUSTOM_LAST      = 0xF002;

static void copy_to_clipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (auto* p = static_cast<wchar_t*>(GlobalLock(h))) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(h);
            if (!SetClipboardData(CF_UNICODETEXT, h)) GlobalFree(h);
        } else {
            GlobalFree(h);
        }
    }
    CloseClipboard();
}

// Open Explorer on the item's parent folder with the item highlighted,
// same behavior as "Show in folder" / "打开文件位置".
static void open_and_select(PCIDLIST_ABSOLUTE item_pidl) {
    // Passing cidl=0 + apidl=NULL causes the shell to treat `item_pidl` itself
    // as the item to select within its parent folder.
    SHOpenFolderAndSelectItems(item_pidl, 0, nullptr, 0);
}

// Find the menu position of the shell's primary "open" item. Prefers the
// entry whose canonical verb is "open"; falls back to the default (bold)
// item. Returns -1 if nothing matches.
static int find_shell_open_index(HMENU menu, IContextMenu* cm) {
    int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        UINT id = GetMenuItemID(menu, i);
        if (id == static_cast<UINT>(-1)) continue;
        if (id < 1 || id > 0x7FFF) continue;
        wchar_t verb[64] = {};
        HRESULT h = cm->GetCommandString(
            static_cast<UINT_PTR>(id - 1), GCS_VERBW, nullptr,
            reinterpret_cast<LPSTR>(verb),
            static_cast<UINT>(sizeof(verb) / sizeof(wchar_t)));
        if (SUCCEEDED(h) && _wcsicmp(verb, L"open") == 0) return i;
    }
    UINT default_id = GetMenuDefaultItem(menu, FALSE, 0);
    if (default_id != static_cast<UINT>(-1) &&
        default_id >= 1 && default_id <= 0x7FFF) {
        for (int i = 0; i < count; ++i) {
            if (GetMenuItemID(menu, i) == default_id) return i;
        }
    }
    return -1;
}

// ── Rename support ─────────────────────────────────────────────
// The shell's "rename" verb needs a SysListView32 / SysTreeView32 host to
// enter inline edit mode; our results live in WebView2 HTML. We intercept
// the verb and let the web UI handle the inline edit UX, then the JS side
// posts back `doRename|<old>|<new>` and we run the rename here.
//
// Rename a file/folder using IFileOperation so we get proper UAC elevation,
// name-conflict dialogs, and support for Explorer's Ctrl+Z undo stack.
static bool rename_item(HWND owner, const std::wstring& path,
                        const std::wstring& new_name) {
    ComPtr<IFileOperation> op;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&op));
    if (FAILED(hr)) return false;

    op->SetOperationFlags(FOFX_ADDUNDORECORD | FOF_ALLOWUNDO);
    op->SetOwnerWindow(owner);

    ComPtr<IShellItem> item;
    hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (FAILED(hr)) return false;

    hr = op->RenameItem(item.Get(), new_name.c_str(), nullptr);
    if (FAILED(hr)) return false;

    hr = op->PerformOperations();
    if (FAILED(hr)) return false;

    BOOL aborted = FALSE;
    op->GetAnyOperationsAborted(&aborted);
    return !aborted;
}

// Move the shell's native "open" item to position 0, preserving its id,
// caption (with accelerator), and MFS_DEFAULT state so it stays bold and
// InvokeCommand keeps working. Returns true on success.
static bool promote_shell_open_to_top(HMENU menu, IContextMenu* cm) {
    int pos = find_shell_open_index(menu, cm);
    if (pos <= 0) return pos == 0;  // already at top, or not found

    wchar_t text[256] = {};
    MENUITEMINFOW mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask  = MIIM_ID | MIIM_STRING | MIIM_FTYPE | MIIM_STATE;
    mii.dwTypeData = text;
    mii.cch = static_cast<UINT>(sizeof(text) / sizeof(wchar_t) - 1);
    if (!GetMenuItemInfoW(menu, pos, TRUE, &mii)) return false;

    DeleteMenu(menu, pos, MF_BYPOSITION);

    // Reuse the captured MENUITEMINFO for the insert; cch is ignored on write.
    mii.dwTypeData = text;
    if (!InsertMenuItemW(menu, 0, TRUE, &mii)) return false;
    return true;
}

// Delete consecutive separators and any leading/trailing separators left
// after a removal — keeps the menu visually clean.
static void compact_separators(HMENU menu) {
    int count = GetMenuItemCount(menu);
    // Remove leading separators.
    while (count > 0 && GetMenuItemID(menu, 0) == static_cast<UINT>(-1)) {
        DeleteMenu(menu, 0, MF_BYPOSITION);
        --count;
    }
    // Remove trailing separators.
    while (count > 0 && GetMenuItemID(menu, count - 1) == static_cast<UINT>(-1)) {
        DeleteMenu(menu, count - 1, MF_BYPOSITION);
        --count;
    }
    // Collapse adjacent separators.
    for (int i = 0; i < count - 1; ) {
        if (GetMenuItemID(menu, i) == static_cast<UINT>(-1) &&
            GetMenuItemID(menu, i + 1) == static_cast<UINT>(-1)) {
            DeleteMenu(menu, i + 1, MF_BYPOSITION);
            --count;
        } else {
            ++i;
        }
    }
}

static void show_shell_context_menu(HWND hwnd, const std::wstring& path_in,
                                    int client_x, int client_y, bool shift) {
    if (path_in.empty()) return;
    std::wstring path = normalize_path(path_in);

    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF sfgao = 0;
    HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &sfgao);
    if (FAILED(hr) || !pidl) return;

    ComPtr<IShellFolder> parent_folder;
    PCUITEMID_CHILD child_pidl = nullptr;
    hr = SHBindToParent(pidl, IID_PPV_ARGS(&parent_folder), &child_pidl);
    if (FAILED(hr) || !parent_folder) {
        CoTaskMemFree(pidl);
        return;
    }

    ComPtr<IContextMenu> cm;
    hr = parent_folder->GetUIObjectOf(
        hwnd, 1,
        reinterpret_cast<PCUITEMID_CHILD_ARRAY>(&child_pidl),
        IID_IContextMenu, nullptr,
        reinterpret_cast<void**>(cm.GetAddressOf()));
    if (FAILED(hr) || !cm) {
        CoTaskMemFree(pidl);
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (!menu) {
        CoTaskMemFree(pidl);
        return;
    }

    UINT flags = CMF_NORMAL | CMF_CANRENAME;
    if (shift) flags |= CMF_EXTENDEDVERBS;
    // GetKeyState picks up the live shift state too, in case JS missed it.
    if (GetKeyState(VK_SHIFT) < 0) flags |= CMF_EXTENDEDVERBS;

    // Let the shell populate the menu first. Many shell extensions ignore
    // indexMenu and splice themselves at position 0, so we can't reliably
    // pre-insert our items and expect them to stay on top. We instead promote
    // the shell's native "open" to the top and insert our two custom items
    // directly below it.
    hr = cm->QueryContextMenu(menu, 0, 1, 0x7FFF, flags);
    if (FAILED(hr)) {
        DestroyMenu(menu);
        CoTaskMemFree(pidl);
        return;
    }

    // Step 1: move the shell's native "打开" to position 0 (keeps shell id,
    // caption with accelerator, and bold default state).
    bool has_shell_open = promote_shell_open_to_top(menu, cm.Get());
    int insert_at = has_shell_open ? 1 : 0;

    // Step 2: insert our two custom items right below the shell's "open".
    InsertMenuW(menu, insert_at,     MF_BYPOSITION | MF_STRING, IDM_OPEN_LOCATION,  L"打开路径");
    InsertMenuW(menu, insert_at + 1, MF_BYPOSITION | MF_STRING, IDM_COPY_FULL_PATH, L"复制文件路径和文件名");
    InsertMenuW(menu, insert_at + 2, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

    compact_separators(menu);

    cm.As(&g_cm2);
    cm.As(&g_cm3);

    POINT pt = {client_x, client_y};
    ClientToScreen(hwnd, &pt);

    // TPM_RETURNCMD makes TrackPopupMenuEx return the selected id instead of
    // posting WM_COMMAND; InvokeCommand is then called directly.
    UINT cmd = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, hwnd, nullptr);

    g_cm3.Reset();
    g_cm2.Reset();

    if (cmd >= IDM_CUSTOM_FIRST && cmd <= IDM_CUSTOM_LAST) {
        switch (cmd) {
        case IDM_OPEN_LOCATION:
            open_and_select(pidl);
            break;
        case IDM_COPY_FULL_PATH:
            copy_to_clipboard(hwnd, path);
            break;
        }
    } else if (cmd > 0) {
        UINT shell_offset = cmd - 1;

        // Some shell verbs (notably "rename") require a SysListView32 /
        // SysTreeView32 host attached to hwnd to enter inline edit mode.
        // Our results live in WebView2 HTML, so letting the shell handle it
        // silently does nothing. Intercept and run our own prompt+rename.
        wchar_t verb[64] = {};
        HRESULT vhr = cm->GetCommandString(
            shell_offset, GCS_VERBW, nullptr,
            reinterpret_cast<LPSTR>(verb),
            sizeof(verb) / sizeof(wchar_t));
        bool handled = false;
        if (SUCCEEDED(vhr) && _wcsicmp(verb, L"rename") == 0) {
            handled = true;
            // Ask the web UI to start inline editing on the corresponding row.
            // The actual file operation happens when JS posts back doRename.
            if (g_webview) {
                std::wstring begin_msg = L"beginRename|" + path;
                g_webview->PostWebMessageAsString(begin_msg.c_str());
            }
        }

        if (!handled) {
            CMINVOKECOMMANDINFOEX ici = {};
            ici.cbSize = sizeof(ici);
            ici.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
            ici.hwnd = hwnd;
            ici.lpVerb  = MAKEINTRESOURCEA(shell_offset);
            ici.lpVerbW = MAKEINTRESOURCEW(shell_offset);
            ici.nShow = SW_SHOWNORMAL;
            ici.ptInvoke = pt;
            cm->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&ici));
        }
    }

    DestroyMenu(menu);
    CoTaskMemFree(pidl);
}

// ── Tray icon helpers ────────────────────────────────────────

static void install_tray_icon(HWND hwnd, HINSTANCE inst) {
    if (g_tray_installed) return;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = TRAY_UID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = static_cast<HICON>(LoadImageW(
        inst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"FastSearch");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
    g_tray_installed = true;
}

static void remove_tray_icon() {
    if (!g_tray_installed) return;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_tray_installed = false;
}

static void show_main_window(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) ShowWindow(hwnd, SW_SHOW);
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    else ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
}

// Show a balloon tip the first time the user minimizes to tray so they
// realize the app is still running in the notification area.
static void hint_tray_balloon() {
    if (!g_tray_installed) return;
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"FastSearch");
    wcscpy_s(nid.szInfo, L"已最小化到托盘，右键图标可恢复或退出。");
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void show_tray_menu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW, L"打开窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"退出程序");
    SetMenuDefaultItem(menu, IDM_TRAY_SHOW, FALSE);
    // Per MSDN: foreground the owner window so the menu dismisses correctly
    // when the user clicks elsewhere. Post WM_NULL afterwards for the same.
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(menu,
                     TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_RIGHTALIGN,
                     pt.x, pt.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // Forward menu messages to the live shell context menu (if any) so shell
    // extensions with submenus / owner-draw items work correctly.
    if (g_cm3) {
        LRESULT lres = 0;
        if (msg == WM_MENUCHAR || msg == WM_INITMENUPOPUP ||
            msg == WM_MEASUREITEM || msg == WM_DRAWITEM) {
            if (SUCCEEDED(g_cm3->HandleMenuMsg2(msg, wp, lp, &lres)))
                return lres;
        }
    } else if (g_cm2) {
        if (msg == WM_INITMENUPOPUP || msg == WM_MEASUREITEM || msg == WM_DRAWITEM) {
            if (SUCCEEDED(g_cm2->HandleMenuMsg(msg, wp, lp)))
                return (msg == WM_INITMENUPOPUP) ? 0 : TRUE;
        }
    }

    if (g_wm_singleton_activate != 0 && msg == g_wm_singleton_activate) {
        show_main_window(hwnd);
        return 0;
    }

    switch (msg) {
    case WM_SIZE:
        resize_webview(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 640;
        mmi->ptMinTrackSize.y = 480;
        return 0;
    }
    case WM_CLOSE:
        // Clicking the window's close button hides to the tray instead of
        // quitting. Real exit only happens via the tray menu's "退出程序".
        if (!g_really_exit) {
            ShowWindow(hwnd, SW_HIDE);
            if (g_first_hide) {
                g_first_hide = false;
                hint_tray_balloon();
            }
            return 0;
        }
        break;
    case WM_TRAYICON:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            show_main_window(hwnd);
            return 0;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            show_tray_menu(hwnd);
            return 0;
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TRAY_SHOW:
            show_main_window(hwnd);
            return 0;
        case IDM_TRAY_EXIT:
            g_really_exit = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_DESTROY:
        remove_tray_icon();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void parse_args() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--url") == 0 && i + 1 < argc) {
            g_url = argv[++i];
        }
    }
    LocalFree(argv);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    g_wm_singleton_activate = RegisterWindowMessageW(SINGLETON_MSG_NAME);
    if (g_wm_singleton_activate == 0) return 1;

    HANDLE instance_mutex = CreateMutexW(nullptr, FALSE, SINGLETON_MUTEX_NAME);
    if (!instance_mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        for (int n = 0; n < 50; n++) {
            HWND other = FindWindowW(WINDOW_CLASS, WINDOW_TITLE);
            if (other) {
                PostMessageW(other, g_wm_singleton_activate, 0, 0);
                CloseHandle(instance_mutex);
                return 0;
            }
            Sleep(100);
        }
        CloseHandle(instance_mutex);
        return 0;
    }
    // First instance: keep the mutex object alive until the process exits.

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    parse_args();

    HICON app_icon_big = static_cast<HICON>(LoadImageW(
        hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    HICON app_icon_small = static_cast<HICON>(LoadImageW(
        hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    if (!app_icon_big) app_icon_big = LoadIconW(nullptr, IDI_APPLICATION);
    if (!app_icon_small) app_icon_small = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 17, 23));
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = app_icon_big;
    wc.hIconSm = app_icon_small;
    RegisterClassExW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenW - DEFAULT_WIDTH) / 2;
    int y = (screenH - DEFAULT_HEIGHT) / 2;

    g_hwnd = CreateWindowExW(
        0, WINDOW_CLASS, WINDOW_TITLE,
        WS_OVERLAPPEDWINDOW,
        x, y, DEFAULT_WIDTH, DEFAULT_HEIGHT,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);

    install_tray_icon(g_hwnd, hInst);

    init_webview(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_webview = nullptr;
    g_controller = nullptr;
    CoUninitialize();
    return 0;
}
