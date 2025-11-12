#ifdef _WIN32

#include "windows/desktop_overlay/desktop_overlay.h"
#include "windows/resources/resource.h"

#include <Shlwapi.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <Strsafe.h>
#include <comdef.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <vector>

#include "utils/cpp_logger.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

namespace screamrouter::desktop {

namespace {
constexpr wchar_t kWindowClassName[] = L"ScreamRouterDesktopOverlayWindow";
constexpr wchar_t kTrayTooltip[] = L"ScreamRouter Desktop Menu";
static UINT g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
constexpr wchar_t kJsHelper[] = LR"JS(
    function isPointOverBody(x, y) {
        const el = document.elementFromPoint(x, y);

        // If no element, consider it as body (transparent area)
        if (!el) {
            return true;
        }

        // If it's body or html element, it's transparent area
        if (el === document.body || el === document.documentElement) {
            return true;
        }

        // If it's the root div without actual content, it's transparent
        if (el.id === 'root') {
            // Check if root has any visible children
            const hasVisibleChildren = el.children.length > 0;
            if (!hasVisibleChildren) {
                return true;
            }
        }

        // If parent is root and element has no substantial content, consider it transparent
        if (el.parentNode && el.parentNode.id === 'root') {
            // This is a direct child of root, likely background
            return true;
        }

        // Modal overlays and content should be interactive (not body)
        if (el.classList) {
            if (el.classList.contains('chakra-modal__overlay') ||
                el.classList.contains('chakra-modal__content-container') ||
                el.classList.contains('chakra-modal__body')) {
                return false;  // These are interactive elements
            }
        }

        // Default: if we hit any other element, it's interactive content
        return false;
    }
)JS";

static COLORREF ExtractColor(ICoreWebView2Environment* /*env*/) {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        return static_cast<COLORREF>(color);
    }
    return RGB(0, 120, 215);
}

DesktopOverlayController* GetController(HWND hwnd) {
    return reinterpret_cast<DesktopOverlayController*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

}  // namespace

DesktopOverlayController::DesktopOverlayController() {
    ZeroMemory(&nid_, sizeof(nid_));

    // Try to load icon from resources first
    tray_icon_ = LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_SCREAMROUTER_ICON));

    // If that fails, try loading from the current module (in case we're in a DLL/PYD)
    if (!tray_icon_) {
        HMODULE hModule = nullptr;
        // Get handle to the current module (DLL/PYD)
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                          GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&DesktopOverlayController::DesktopOverlayController),
                          &hModule);
        if (hModule) {
            tray_icon_ = LoadIconW(hModule, MAKEINTRESOURCEW(IDI_SCREAMROUTER_ICON));
            if (tray_icon_) {
                LOG_CPP_INFO("Loaded ScreamRouter icon from module resources");
            }
        }
    }

    // If still no icon, try loading from file directly
    if (!tray_icon_) {
        // Try to load from a relative path
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring exeDir(exePath);
        size_t lastSlash = exeDir.find_last_of(L"\\/");
        if (lastSlash != std::wstring::npos) {
            exeDir = exeDir.substr(0, lastSlash);

            // Try several possible icon locations
            std::vector<std::wstring> iconPaths = {
                exeDir + L"\\screamrouter.ico",
                exeDir + L"\\resources\\screamrouter.ico",
                exeDir + L"\\..\\resources\\screamrouter.ico",
                exeDir + L"\\src\\audio_engine\\windows\\resources\\screamrouter.ico",
                L"C:\\screamrouter\\src\\audio_engine\\windows\\resources\\screamrouter.ico"
            };

            for (const auto& iconPath : iconPaths) {
                tray_icon_ = (HICON)LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON,
                                               0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
                if (tray_icon_) {
                    LOG_CPP_INFO("Loaded ScreamRouter icon from file: %ls", iconPath.c_str());
                    break;
                }
            }
        }
    }

    // Final fallback to default icon
    if (!tray_icon_) {
        LOG_CPP_WARNING("Failed to load ScreamRouter icon from any source, using default");
        tray_icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }
}

DesktopOverlayController::~DesktopOverlayController() {
    Shutdown();
}

bool DesktopOverlayController::Start(const std::wstring& url, int width, int height) {
    if (running_.load()) {
        LOG_CPP_WARNING("DesktopOverlay Start requested while already running");
        return true;
    }

    LOG_CPP_INFO("DesktopOverlay starting (url=%ls width=%d height=%d)", url.c_str(), width, height);
    width_ = width > 0 ? width : kDefaultWidth;
    height_ = height > 0 ? height : kDefaultHeight;
    running_.store(true);

    ui_thread_ = std::thread(&DesktopOverlayController::UiThreadMain, this, url, width_, height_);
    return true;
}

void DesktopOverlayController::Show() {
    LOG_CPP_DEBUG("DesktopOverlay::Show");
    if (!ready_.load()) {
        LOG_CPP_DEBUG("DesktopOverlay not ready; Show deferred");
        return;
    }
    HWND hwnd = window_;
    if (hwnd) {
        PostMessage(hwnd, kControlMessage, static_cast<WPARAM>(ControlCommand::kShow), 0);
    }
}

void DesktopOverlayController::Hide() {
    LOG_CPP_DEBUG("DesktopOverlay::Hide");
    if (!ready_.load()) {
        return;
    }
    HWND hwnd = window_;
    if (hwnd) {
        PostMessage(hwnd, kControlMessage, static_cast<WPARAM>(ControlCommand::kHide), 0);
    }
}

void DesktopOverlayController::Toggle() {
    LOG_CPP_DEBUG("DesktopOverlay::Toggle");
    if (!ready_.load()) {
        LOG_CPP_DEBUG("DesktopOverlay not ready; toggle ignored");
        return;
    }
    HWND hwnd = window_;
    if (hwnd) {
        PostMessage(hwnd, kControlMessage, static_cast<WPARAM>(ControlCommand::kToggle), 0);
    }
}

void DesktopOverlayController::Shutdown() {
    if (!running_.load()) {
        return;
    }
    LOG_CPP_INFO("DesktopOverlay shutdown requested");
    running_.store(false);
    HWND hwnd = window_;
    if (hwnd) {
        PostMessage(hwnd, kControlMessage, static_cast<WPARAM>(ControlCommand::kShutdown), 0);
    }
    if (ui_thread_.joinable()) {
        ui_thread_.join();
    }
    window_ = nullptr;
}

void DesktopOverlayController::UiThreadMain(std::wstring url, int width, int height) {
    hinstance_ = GetModuleHandle(nullptr);
    LOG_CPP_INFO("DesktopOverlay UI thread starting");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    srand(static_cast<unsigned int>(GetTickCount64()));
    std::wstring class_name = std::wstring(kWindowClassName) + L"_" + std::to_wstring(rand());

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = class_name.c_str();
    if (!RegisterClassExW(&wc)) {
        LOG_CPP_ERROR("DesktopOverlay failed to register class (err=%lu)", GetLastError());
        running_.store(false);
        CoUninitialize();
        return;
    }

    RECT work_area = GetWorkArea();
    const int margin_x = 16;
    const int margin_y = 8;
    const int work_w = work_area.right - work_area.left;
    const int work_h = work_area.bottom - work_area.top;
    const int usable_w = std::max(work_w - margin_x * 2, 360);
    width_ = width > 0 ? std::min(width, usable_w)
                       : std::clamp(usable_w, 420, 640);
    // Use almost full height of working area minus margins
    height_ = height > 0 ? std::min(height, work_h - margin_y * 2)
                         : work_h - margin_y * 2;
    // Position at bottom-right corner
    const int left = work_area.right - width_ - margin_x;
    const int top = work_area.bottom - height_ - margin_y;

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,  // No WS_EX_TRANSPARENT initially
        class_name.c_str(),
        L"ScreamRouter Desktop Menu",
        WS_POPUP,
        left,
        top,
        width_,
        height_,
        nullptr,
        nullptr,
        hinstance_,
        this);

    if (!hwnd) {
        LOG_CPP_ERROR("DesktopOverlay failed to create window (err=%lu)", GetLastError());
        running_.store(false);
        CoUninitialize();
        return;
    }

    window_ = hwnd;
    LOG_CPP_INFO("DesktopOverlay window created (hwnd=%p)", hwnd);
    url_ = url;

    // Set up the window as a layered window with transparency
    LONG ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
    // Use color key transparency - black pixels (RGB(0,0,0)) will be transparent
    if (!SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY | LWA_ALPHA)) {
        LOG_CPP_WARNING("DesktopOverlay failed to set layered window attributes (err=%lu)", GetLastError());
    }
    // Extend frame into client area for glass effect
    MARGINS margins = { -1 };
    HRESULT dwm_hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (FAILED(dwm_hr)) {
        LOG_CPP_WARNING("DesktopOverlay failed to extend frame (hr=0x%08X)", dwm_hr);
    }
    ShowWindow(hwnd, SW_HIDE);

    // Initialize with mouse enabled (interactive)
    mouse_disabled_ = false;

    PositionWindow();

    EnsureTrayIcon();
    LOG_CPP_INFO("DesktopOverlay tray icon initialized");

    SetTimer(hwnd, kMouseTimerId, 50, nullptr);
    SetTimer(hwnd, kColorTimerId, 5000, nullptr);
    LOG_CPP_DEBUG("DesktopOverlay timers started");

    InitWebView();

    ready_.store(true);

    MSG msg;
    while (running_.load() && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CleanupTrayIcon();
    if (webview_controller_) {
        webview_controller_->Close();
        webview_controller_.Reset();
    }
    webview_.Reset();
    KillTimer(hwnd, kMouseTimerId);
    KillTimer(hwnd, kColorTimerId);
    ready_.store(false);
    DestroyWindow(hwnd);
    window_ = nullptr;
    CoUninitialize();
    UnregisterClassW(class_name.c_str(), hinstance_);
    LOG_CPP_INFO("DesktopOverlay UI thread exiting");
}

void DesktopOverlayController::InitWebView() {
    LOG_CPP_INFO("DesktopOverlay initializing WebView2");

    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--ignore-certificate-errors");

    std::wstring user_data_folder;
    PWSTR local_appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_appdata))) {
        std::filesystem::path user_dir(local_appdata);
        CoTaskMemFree(local_appdata);
        user_dir /= L"ScreamRouter";
        user_dir /= L"DesktopOverlay";
        user_dir /= L"WebView2";
        std::error_code ec;
        std::filesystem::create_directories(user_dir, ec);
        if (!ec) {
            user_data_folder = user_dir.wstring();
        } else {
            LOG_CPP_WARNING("DesktopOverlay failed to create WebView2 user data dir (code=%d)", static_cast<int>(ec.value()));
        }
    }

    auto handler = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [this](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
                LOG_CPP_ERROR("DesktopOverlay WebView2 environment creation failed (hr=0x%08X)", result);
                return result;
            }
            webview_env_ = env;
            accent_color_ = ExtractColor(env);

            auto controllerHandler = Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this](HRESULT createResult, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(createResult) || !controller) {
                        LOG_CPP_ERROR("DesktopOverlay WebView2 controller creation failed (hr=0x%08X)", createResult);
                        return createResult;
                    }
                    webview_controller_ = controller;
                    webview_controller_->get_CoreWebView2(&webview_);
                    // Set WebView to fill entire client area
                    RECT bounds{};
                    GetClientRect(window_, &bounds);
                    LOG_CPP_DEBUG("DesktopOverlay setting WebView bounds: %d,%d %dx%d",
                                  bounds.left, bounds.top,
                                  bounds.right - bounds.left,
                                  bounds.bottom - bounds.top);
                    webview_controller_->put_Bounds(bounds);

                    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                    if (SUCCEEDED(webview_controller_.As(&controller2)) && controller2) {
                        COREWEBVIEW2_COLOR color{};
                        color.A = 0;
                        color.R = 0;
                        color.G = 0;
                        color.B = 0;
                        controller2->put_DefaultBackgroundColor(color);
                    } else {
                        LOG_CPP_WARNING("WebView2 controller does not support DefaultBackgroundColor; relying on CSS script");
                    }

                    webview_controller_->put_IsVisible(TRUE);

                    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                    if (SUCCEEDED(webview_->get_Settings(&settings)) && settings) {
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->put_AreDefaultContextMenusEnabled(TRUE);
                        settings->put_IsZoomControlEnabled(FALSE);
                        settings->put_AreDevToolsEnabled(TRUE);
                        settings->put_IsBuiltInErrorPageEnabled(FALSE);
                    }

                    InjectHelpers();
                    Navigate();
                    LOG_CPP_INFO("DesktopOverlay WebView2 initialized successfully");
                    return S_OK;
                });

            env->CreateCoreWebView2Controller(window_, controllerHandler.Get());
            return S_OK;
        });

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        user_data_folder.empty() ? nullptr : user_data_folder.c_str(),
        nullptr,
        handler.Get());
    if (FAILED(hr)) {
        LOG_CPP_ERROR("DesktopOverlay CreateCoreWebView2EnvironmentWithOptions returned hr=0x%08X", hr);
    } else {
        LOG_CPP_DEBUG("DesktopOverlay requested WebView2 environment creation");
    }
}

void DesktopOverlayController::InjectHelpers() {
    if (!webview_) {
        return;
    }
    webview_->AddScriptToExecuteOnDocumentCreated(kJsHelper, nullptr);
}

void DesktopOverlayController::Navigate() {
    if (webview_ && !url_.empty()) {
        webview_->Navigate(url_.c_str());
    }
}

void DesktopOverlayController::SendDesktopMenuShow() {
    if (!webview_) {
        return;
    }
    LOG_CPP_DEBUG("DesktopOverlay sending DesktopMenuShow");
    RefreshAccentColor();
    int r = GetRValue(accent_color_);
    int g = GetGValue(accent_color_);
    int b = GetBValue(accent_color_);
    int a = 255;
    wchar_t script[256];
    StringCchPrintfW(script, std::size(script), L"DesktopMenuShow(%d,%d,%d,%d);", r, g, b, a);
    webview_->ExecuteScript(script, nullptr);
}

void DesktopOverlayController::SendDesktopMenuHide() {
    if (webview_) {
        LOG_CPP_DEBUG("DesktopOverlay sending DesktopMenuHide");
        webview_->ExecuteScript(L"DesktopMenuHide();", nullptr);
    }
}

void DesktopOverlayController::RefreshAccentColor() {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        accent_color_ = static_cast<COLORREF>(color);
    }
}

void DesktopOverlayController::DisableMouse() {
    if (mouse_disabled_ || !window_) {
        return;
    }
    LOG_CPP_DEBUG("DesktopOverlay disabling mouse (making pass-through)");

    // Match C# implementation: ensure WS_EX_LAYERED is set
    LONG style = GetWindowLong(window_, GWL_EXSTYLE);
    style |= WS_EX_LAYERED;
    SetWindowLong(window_, GWL_EXSTYLE, style);

    // Set layered attributes for transparency
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);

    // Now add WS_EX_TRANSPARENT to make it click-through
    style = GetWindowLong(window_, GWL_EXSTYLE);
    style |= WS_EX_TRANSPARENT;
    SetWindowLong(window_, GWL_EXSTYLE, style);

    // Force the window to update with the new style
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    mouse_disabled_ = true;
    LOG_CPP_DEBUG("DesktopOverlay mouse disabled (pass-through) - style=0x%08X", style);
}

void DesktopOverlayController::EnableMouse() {
    if (!mouse_disabled_ || !window_) {
        return;
    }
    LOG_CPP_DEBUG("DesktopOverlay enabling mouse (making interactive)");

    // Match C# implementation more closely
    LONG style = GetWindowLong(window_, GWL_EXSTYLE);

    // Remove WS_EX_TRANSPARENT to allow clicks
    style &= ~WS_EX_TRANSPARENT;

    // C# actually removes WS_EX_LAYERED when enabling mouse!
    // This is the key difference
    if (style & WS_EX_LAYERED) {
        style &= ~WS_EX_LAYERED;
    }

    SetWindowLong(window_, GWL_EXSTYLE, style);

    // Still set opacity to full
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);

    // Force the window to update with the new style
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    mouse_disabled_ = false;
    LOG_CPP_DEBUG("DesktopOverlay mouse enabled (interactive) - style=0x%08X", style);
}

void DesktopOverlayController::HandleMouseTimer() {
    if (!webview_ || script_pending_) {
        return;
    }

    POINT pt{};
    GetCursorPos(&pt);
    if (pt.x == last_mouse_.x && pt.y == last_mouse_.y) {
        return;
    }
    last_mouse_ = pt;

    RECT rect{};
    GetWindowRect(window_, &rect);
    if (!PtInRect(&rect, pt)) {
        return;
    }

    POINT client_pt = pt;
    ScreenToClient(window_, &client_pt);
    UINT dpi = GetDpiForWindow(window_);
    const float scale = dpi / 96.0f;
    const int scaled_x = static_cast<int>(client_pt.x / scale);
    const int scaled_y = static_cast<int>(client_pt.y / scale);

    std::wstring script = L"(function(){return isPointOverBody(" +
                          std::to_wstring(scaled_x) + L"," + std::to_wstring(scaled_y) +
                          L");})()";

    script_pending_ = true;
    webview_->ExecuteScript(
        script.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, scaled_x, scaled_y](HRESULT error, PCWSTR result) -> HRESULT {
                script_pending_ = false;
                if (FAILED(error) || !result) {
                    LOG_CPP_WARNING("DesktopOverlay hit-test script failed (hr=0x%08X)", error);
                    return S_OK;
                }
                bool over_body = (wcscmp(result, L"true") == 0) || (wcscmp(result, L"\"true\"") == 0);
                LOG_CPP_DEBUG("DesktopOverlay hit-test at (%d,%d) result='%ls' over_body=%d mouse_disabled=%d",
                              scaled_x, scaled_y, result, over_body, mouse_disabled_);

                // over_body=true means we're over transparent area, should disable mouse (pass-through)
                // over_body=false means we're over content, should enable mouse (interactive)
                if (over_body && !mouse_disabled_) {
                    LOG_CPP_INFO("DesktopOverlay detected transparent area, disabling mouse");
                    DisableMouse();
                } else if (!over_body && mouse_disabled_) {
                    LOG_CPP_INFO("DesktopOverlay detected content area, enabling mouse");
                    EnableMouse();
                }
                return S_OK;
            })
            .Get());
}

void DesktopOverlayController::HandleColorTimer() {
    RefreshAccentColor();
}

void DesktopOverlayController::EnsureTrayIcon() {
    if (!window_) {
        return;
    }

    NOTIFYICONDATAW remove{};
    remove.cbSize = sizeof(remove);
    remove.hWnd = window_;
    remove.uID = kTrayIconId;
    remove.guidItem = tray_guid_;
    remove.uFlags = NIF_GUID;
    Shell_NotifyIconW(NIM_DELETE, &remove);

    NOTIFYICONDATAW add{};
    add.cbSize = sizeof(add);
    add.hWnd = window_;
    add.uID = kTrayIconId;
    add.guidItem = tray_guid_;
    add.uCallbackMessage = kTrayCallbackMessage;
    add.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;
    add.hIcon = tray_icon_ ? tray_icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    StringCchCopyW(add.szTip, ARRAYSIZE(add.szTip), kTrayTooltip);

    if (!Shell_NotifyIconW(NIM_ADD, &add)) {
        LOG_CPP_ERROR("DesktopOverlay failed to add tray icon (err=%lu)", GetLastError());
        return;
    }

    add.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &add);

    nid_ = add;
}

void DesktopOverlayController::CleanupTrayIcon() {
    NOTIFYICONDATAW remove{};
    remove.cbSize = sizeof(remove);
    remove.hWnd = window_;
    remove.uID = kTrayIconId;
    remove.guidItem = tray_guid_;
    remove.uFlags = NIF_GUID;
    Shell_NotifyIconW(NIM_DELETE, &remove);
    nid_.cbSize = 0;
    if (tray_menu_) {
        DestroyMenu(tray_menu_);
        tray_menu_ = nullptr;
    }
}

void DesktopOverlayController::BuildTrayMenu() {
    if (tray_menu_) {
        DestroyMenu(tray_menu_);
    }
    tray_menu_ = CreatePopupMenu();
    AppendMenuW(tray_menu_, MF_STRING, static_cast<UINT>(TrayCommand::kToggle), L"Toggle Desktop Menu");
    AppendMenuW(tray_menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tray_menu_, MF_STRING, static_cast<UINT>(TrayCommand::kExit), L"Exit ScreamRouter");
}

void DesktopOverlayController::ShowTrayMenu(const POINT& anchor) {
    if (!tray_menu_) {
        BuildTrayMenu();
    }

    POINT pt = anchor;
    if (pt.x == 0 && pt.y == 0) {
        GetCursorPos(&pt);
    }
    SetForegroundWindow(window_);
    TrackPopupMenuEx(tray_menu_, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, window_, nullptr);

    NOTIFYICONDATAW focus{};
    focus.cbSize = sizeof(focus);
    focus.hWnd = window_;
    focus.uID = kTrayIconId;
    focus.guidItem = tray_guid_;
    focus.uFlags = NIF_GUID;
    Shell_NotifyIconW(NIM_SETFOCUS, &focus);
}

void DesktopOverlayController::HandleTrayEvent(WPARAM wparam, LPARAM lparam) {
    UINT icon_id = HIWORD(lparam);
    if (icon_id != kTrayIconId) {
        LOG_CPP_DEBUG("DesktopOverlay tray event for different icon (%u)", icon_id);
        return;
    }
    UINT event = LOWORD(lparam);
    DWORD_PTR anchor_raw = static_cast<DWORD_PTR>(wparam);
    bool has_anchor = anchor_raw != 0;
    POINT anchor{
        GET_X_LPARAM(static_cast<LPARAM>(anchor_raw)),
        GET_Y_LPARAM(static_cast<LPARAM>(anchor_raw))
    };
    if (!has_anchor) {
        GetCursorPos(&anchor);
    }

    switch (event) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        LOG_CPP_INFO("DesktopOverlay tray left-click activation (event=0x%04x)", event);
        Toggle();
        break;
    case NIN_SELECT:
    case NIN_KEYSELECT:
        LOG_CPP_INFO("DesktopOverlay tray activation (event=0x%04x)", event);
        Toggle();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        LOG_CPP_INFO("DesktopOverlay tray context request (event=0x%04x)", event);
        ShowTrayMenu(anchor);
        break;
    case NIN_POPUPOPEN:
    case NIN_POPUPCLOSE:
        LOG_CPP_DEBUG("DesktopOverlay tray popup state change (event=0x%04x)", event);
        break;
    default:
        LOG_CPP_DEBUG("DesktopOverlay tray event ignored (event=0x%04x)", event);
        break;
    }
}

void DesktopOverlayController::UpdateWebViewBounds() {
    if (!window_ || !webview_controller_) {
        return;
    }
    RECT bounds{};
    GetClientRect(window_, &bounds);
    LOG_CPP_DEBUG("DesktopOverlay updating WebView bounds: %d,%d %dx%d",
                  bounds.left, bounds.top,
                  bounds.right - bounds.left,
                  bounds.bottom - bounds.top);
    webview_controller_->put_Bounds(bounds);
}

void DesktopOverlayController::FocusWebView() {
    if (webview_controller_) {
        webview_controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    }
}

RECT DesktopOverlayController::GetWorkArea() const {
    RECT work{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = 0;
        work.top = 0;
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    return work;
}

void DesktopOverlayController::PositionWindow() {
    if (!window_) {
        return;
    }
    RECT work = GetWorkArea();
    constexpr int margin_x = 16;
    constexpr int margin_y = 8;
    const int work_w = work.right - work.left;
    const int work_h = work.bottom - work.top;
    const int usable_w = std::max(work_w - margin_x * 2, 360);

    // Ensure we're using the full height minus margin (similar to C# implementation)
    width_ = std::clamp(width_, 360, usable_w);
    height_ = std::clamp(height_, 400, work_h - margin_y * 2);  // margin on both top and bottom

    // Position at bottom-right of working area
    int left = work.right - width_ - margin_x;
    int top = work.bottom - height_ - margin_y;  // Position from bottom like C# version

    LOG_CPP_DEBUG("DesktopOverlay positioning window: pos=%d,%d size=%dx%d (work area=%d,%d %dx%d)",
                  left, top, width_, height_,
                  work.left, work.top, work_w, work_h);

    SetWindowPos(window_, nullptr, left, top, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE);
    if (webview_controller_) {
        RECT bounds{0, 0, width_, height_};
        LOG_CPP_DEBUG("DesktopOverlay setting WebView bounds after position: %dx%d",
                      width_, height_);
        webview_controller_->put_Bounds(bounds);
    }
}

void DesktopOverlayController::HandleCommand(WPARAM wparam) {
    const UINT cmd = LOWORD(wparam);
    if (cmd == static_cast<UINT>(TrayCommand::kToggle)) {
        if (IsWindowVisible(window_)) {
            ShowWindow(window_, SW_HIDE);
            SendDesktopMenuHide();
        } else {
            PositionWindow();

            // Force mouse to be enabled initially (interactive)
            mouse_disabled_ = true;  // Set to true so EnableMouse will work
            EnableMouse();

            ShowWindow(window_, SW_SHOWNOACTIVATE);
            SetForegroundWindow(window_);
            SendDesktopMenuShow();
        }
    } else if (cmd == static_cast<UINT>(TrayCommand::kExit)) {
        int response = MessageBoxW(
            window_,
            L"Exit ScreamRouter?",
            L"ScreamRouter Desktop",
            MB_ICONQUESTION | MB_OKCANCEL | MB_TOPMOST | MB_SETFOREGROUND);
        if (response == IDOK) {
            LOG_CPP_INFO("DesktopOverlay exit confirmed via tray");
            CleanupTrayIcon();
            SendDesktopMenuHide();
            PostMessage(window_, kControlMessage, static_cast<WPARAM>(ControlCommand::kShutdown), 0);
            ExitProcess(0);
        } else {
            LOG_CPP_INFO("DesktopOverlay exit canceled");
        }
    }
}

LRESULT CALLBACK DesktopOverlayController::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    DesktopOverlayController* controller = GetController(hwnd);
    if (msg == g_taskbar_created) {
        if (controller) {
            controller->EnsureTrayIcon();
        }
        return 0;
    }
    switch (msg) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<LPCREATESTRUCT>(lparam);
        controller = static_cast<DesktopOverlayController*>(create->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(controller));
        return TRUE;
    }
    case WM_CREATE:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE:
        if (controller) {
            controller->UpdateWebViewBounds();
        }
        return 0;
    case WM_ACTIVATE:
        if (controller && LOWORD(wparam) == WA_INACTIVE) {
            controller->Hide();
        }
        return 0;
    case WM_SETFOCUS:
        if (controller) {
            controller->FocusWebView();
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &ps.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (controller) {
            if (wparam == kMouseTimerId) {
                controller->HandleMouseTimer();
            } else if (wparam == kColorTimerId) {
                controller->HandleColorTimer();
            }
        }
        return 0;
    case WM_COMMAND:
        if (controller) {
            controller->HandleCommand(wparam);
        }
        return 0;
    default:
        break;
    }

    if (msg == kTrayCallbackMessage) {
        if (controller) {
            controller->HandleTrayEvent(wparam, lparam);
        }
        return 0;
    }

    if (msg == kControlMessage) {
        if (controller) {
            switch (static_cast<ControlCommand>(wparam)) {
            case ControlCommand::kShow:
                if (!IsWindowVisible(hwnd)) {
                    controller->PositionWindow();

                    // Force mouse to be enabled initially (interactive)
                    controller->mouse_disabled_ = true;  // Set to true so EnableMouse will work
                    controller->EnableMouse();

                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                    SetForegroundWindow(hwnd);
                    SetFocus(hwnd);
                    controller->FocusWebView();
                    controller->SendDesktopMenuShow();
                    LOG_CPP_INFO("DesktopOverlay shown with mouse enabled");
                }
                break;
            case ControlCommand::kHide:
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    controller->SendDesktopMenuHide();
                    LOG_CPP_INFO("DesktopOverlay hidden");
                }
                break;
            case ControlCommand::kToggle:
                if (IsWindowVisible(hwnd)) {
                    ShowWindow(hwnd, SW_HIDE);
                    controller->SendDesktopMenuHide();
                    LOG_CPP_INFO("DesktopOverlay toggled hidden");
                } else {
                    controller->PositionWindow();

                    // Force mouse to be enabled initially (interactive)
                    controller->mouse_disabled_ = true;  // Set to true so EnableMouse will work
                    controller->EnableMouse();

                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                    SetForegroundWindow(hwnd);
                    SetFocus(hwnd);
                    controller->FocusWebView();
                    controller->SendDesktopMenuShow();
                    LOG_CPP_INFO("DesktopOverlay toggled shown with mouse enabled");
                }
                break;
            case ControlCommand::kShutdown:
                LOG_CPP_INFO("DesktopOverlay shutting down window");
                DestroyWindow(hwnd);
                break;
            }
        }
        return 0;
    }

    return DefWindowProc(hwnd, msg, wparam, lparam);
}

}  // namespace screamrouter::desktop

#endif  // _WIN32
