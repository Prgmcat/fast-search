#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <functional>

#include "WebView2.h"
#include <wrl.h>

using namespace Microsoft::WRL;

static const wchar_t* WINDOW_CLASS = L"FastSearchGUI";
static const wchar_t* WINDOW_TITLE = L"FastSearch";
static const int DEFAULT_WIDTH = 1100;
static const int DEFAULT_HEIGHT = 750;

static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static HWND g_hwnd = nullptr;
static std::wstring g_url = L"http://127.0.0.1:9800";

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

static void init_webview(HWND hwnd) {
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
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

                            resize_webview(hwnd);
                            g_webview->Navigate(g_url.c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
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
    case WM_DESTROY:
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
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    parse_args();

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(15, 17, 23));
    wc.lpszClassName = WINDOW_CLASS;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
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
