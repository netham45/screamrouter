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
        if (!el) { return true; }
        if (el === document.body || el === document.documentElement) { return true; }
        if (el.id === 'root' || (el.parentNode && el.parentNode.id === 'root')) { return true; }
        if (el.classList && (el.classList.contains('chakra-modal__overlay') || el.classList.contains('chakra-modal__content-container') || el.classList.contains('chakra-modal__body'))) {
            return false;
        }
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
    tray_icon_ = LoadIconW(nullptr, IDI_APPLICATION);
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
    const int margen_x = 18;
    const int margen_y = 12;
    const int work_w = work_area.right - work_area.left;
    const int work_h = work_area.bottom - work_area.top;
    const int usable_w = std::max(work_w - margen_x * 2, 360);
    const int usable_h = std::max(work_h - margen_y * 2, 480);
    width_ = width > 0 ? std::min(width, usable_w)
                       : std::clamp(usable_w, 420, 640);
    height_ = height > 0 ? std::min(height, usable_h)
                         : usable_h;
    const int left = work_area.right - width_ - margen_x;
    const int top = work_area.bottom - height_ - margen_y;

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
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

    LONG ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    SetWindowLong(hwnd, GWL_EXSTYLE, ex_style | WS_EX_LAYERED);
    if (!SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY)) {
        LOG_CPP_WARNING("DesktopOverlay failed to set layered window attributes (err=%lu)", GetLastError());
    }
    MARGINS margins = { -1 };
    HRESULT dwm_hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
    if (FAILED(dwm_hr)) {
        LOG_CPP_WARNING("DesktopOverlay failed to extend frame (hr=0x%08X)", dwm_hr);
    }
    ShowWindow(hwnd, SW_HIDE);

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
                    RECT bounds{};
                    GetClientRect(window_, &bounds);
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
    webview_->AddScriptToExecuteOnDocumentCreated(kTransparentBackgroundScript, nullptr);
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

void DesktopOverlayController::SetMouseMode(MouseMode mode) {
    if (mouse_mode_ == mode || !window_) {
        return;
    }
    mouse_mode_ = mode;
    LONG style = GetWindowLong(window_, GWL_EXSTYLE);
    if (mode == MouseMode::kPassthrough) {
        style |= WS_EX_TRANSPARENT;
    } else {
        style &= ~WS_EX_TRANSPARENT;
    }
    SetWindowLong(window_, GWL_EXSTYLE, style);
    SetLayeredWindowAttributes(window_, RGB(0, 0, 0), 0, LWA_COLORKEY);
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
        SetMouseMode(MouseMode::kInteractive);
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
            [this](HRESULT error, PCWSTR result) -> HRESULT {
                script_pending_ = false;
                if (FAILED(error) || !result) {
                    return S_OK;
                }
                bool over_body = (wcscmp(result, L"true") == 0) || (wcscmp(result, L"\"true\"") == 0);
                SetMouseMode(over_body ? MouseMode::kPassthrough : MouseMode::kInteractive);
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
    case NIN_SELECT:
    case NIN_KEYSELECT:
        if (!has_anchor) {
            LOG_CPP_INFO("DesktopOverlay tray keyboard activation (event=0x%04x)", event);
            Toggle();
        } else {
            LOG_CPP_DEBUG("DesktopOverlay tray skipped mouse-generated NIN event (event=0x%04x)", event);
        }
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
    constexpr int margin_x = 18;
    constexpr int margin_y = 12;
    const int work_w = work.right - work.left;
    const int work_h = work.bottom - work.top;
    const int usable_w = std::max(work_w - margin_x * 2, 360);
    const int usable_h = std::max(work_h - margin_y * 2, 480);
    width_ = std::clamp(width_, 360, usable_w);
    height_ = std::clamp(height_, 480, usable_h);
    int left = work.right - width_ - margin_x;
    int top = work.bottom - height_ - margin_y;
    SetWindowPos(window_, nullptr, left, top, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE);
    if (webview_controller_) {
        RECT bounds{0, 0, width_, height_};
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
                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                    SetForegroundWindow(hwnd);
                    controller->SendDesktopMenuShow();
                    LOG_CPP_INFO("DesktopOverlay shown");
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
                    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                    SetForegroundWindow(hwnd);
                    controller->SendDesktopMenuShow();
                    LOG_CPP_INFO("DesktopOverlay toggled shown");
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
