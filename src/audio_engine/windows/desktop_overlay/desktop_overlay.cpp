#ifdef _WIN32

#include "windows/desktop_overlay/desktop_overlay.h"
#include "windows/resources/resource.h"

#include <Shlwapi.h>
#include <Strsafe.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iterator>

#include "utils/cpp_logger.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")

namespace screamrouter::desktop {

namespace {
constexpr wchar_t kWindowClassName[] = L"ScreamRouterDesktopOverlayWindow";
constexpr wchar_t kTrayTooltip[] = L"ScreamRouter Desktop Menu";
#ifndef NIN_POPUPOPEN
#define NIN_POPUPOPEN (WM_USER + 6)
#endif
#ifndef NIN_POPUPCLOSE
#define NIN_POPUPCLOSE (WM_USER + 7)
#endif
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

RECT GetExitButtonRect(HWND button) {
    RECT rect{};
    if (button) {
        GetWindowRect(button, &rect);
    }
    return rect;
}
}  // namespace

DesktopOverlayController::DesktopOverlayController() {
    ZeroMemory(&nid_, sizeof(nid_));
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
    HWND hwnd = window_;
    if (hwnd) {
        PostMessage(hwnd, kControlMessage, static_cast<WPARAM>(ControlCommand::kShow), 0);
    }
}

void DesktopOverlayController::Hide() {
    LOG_CPP_DEBUG("DesktopOverlay::Hide");
    HWND hwnd = window_;
    if (hwnd) {
        PostMessage(hwnd, kControlMessage, static_cast<WPARAM>(ControlCommand::kHide), 0);
    }
}

void DesktopOverlayController::Toggle() {
    LOG_CPP_DEBUG("DesktopOverlay::Toggle");
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

    const int screen_w = GetSystemMetrics(SM_CXSCREEN);
    const int screen_h = GetSystemMetrics(SM_CYSCREEN);
    const int left = (screen_w - width) / 2;
    const int top = screen_h - height - 80;

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        class_name.c_str(),
        L"ScreamRouter Desktop Menu",
        WS_POPUP,
        left,
        top,
        width,
        height,
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

    SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(hwnd, SW_HIDE);

    EnsureTrayIcon();
    LOG_CPP_INFO("DesktopOverlay tray icon initialized");
    CreateExitButton();

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
    DestroyWindow(hwnd);
    window_ = nullptr;
    CoUninitialize();
    LOG_CPP_INFO("DesktopOverlay UI thread exiting");
}

void DesktopOverlayController::InitWebView() {
    LOG_CPP_INFO("DesktopOverlay initializing WebView2");
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

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr, handler.Get());
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
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
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

    if (IsPointInsideExitButton(pt)) {
        SetMouseMode(MouseMode::kInteractive);
        return;
    }

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
    if (nid_.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
    }

    ZeroMemory(&nid_, sizeof(NOTIFYICONDATAW));
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = window_;
    nid_.uID = 1;
    nid_.uVersion = NOTIFYICON_VERSION_4;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = kTrayCallbackMessage;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), kTrayTooltip);
    Shell_NotifyIconW(NIM_ADD, &nid_);
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void DesktopOverlayController::CleanupTrayIcon() {
    if (nid_.cbSize == sizeof(NOTIFYICONDATAW)) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        nid_.cbSize = 0;
    }
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

void DesktopOverlayController::ShowTrayMenu() {
    if (!tray_menu_) {
        BuildTrayMenu();
    }

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(window_);
    TrackPopupMenuEx(tray_menu_, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, window_, nullptr);
}

void DesktopOverlayController::HandleTrayEvent(WPARAM /*wparam*/, LPARAM lparam) {
    UINT msg = LOWORD(lparam);
    UINT metadata = HIWORD(lparam);
    LOG_CPP_DEBUG("DesktopOverlay tray raw event lParam=0x%08lx (msg=0x%04x meta=0x%04x)", lparam, msg, metadata);
    switch (msg) {
    case WM_LBUTTONDOWN:
        LOG_CPP_DEBUG("DesktopOverlay tray WM_LBUTTONDOWN received");
        break;
    case WM_LBUTTONUP:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        LOG_CPP_INFO("DesktopOverlay tray activation (msg=0x%04x)", msg);
        Toggle();
        break;
    case WM_RBUTTONDOWN:
        LOG_CPP_DEBUG("DesktopOverlay tray WM_RBUTTONDOWN received");
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    case NIN_POPUPOPEN:
    case NIN_POPUPCLOSE:
        LOG_CPP_INFO("DesktopOverlay tray context menu request (msg=0x%04x)", msg);
        ShowTrayMenu();
        break;
    default:
        LOG_CPP_DEBUG("DesktopOverlay tray event ignored (msg=0x%04x)", msg);
        break;
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
        CleanupTrayIcon();
        ExitProcess(0);
    }
}

bool DesktopOverlayController::IsPointInsideExitButton(POINT screen_pt) const {
    if (!exit_button_) {
        return false;
    }
    RECT rect{};
    GetWindowRect(exit_button_, &rect);
    return PtInRect(&rect, screen_pt);
}

void DesktopOverlayController::CreateExitButton() {
    if (!window_) {
        return;
    }
    const int button_width = 80;
    const int button_height = 28;
    exit_button_ = CreateWindowExW(
        0,
        L"BUTTON",
        L"Exit",
        WS_CHILD | WS_VISIBLE | BS_FLAT,
        width_ - button_width - 16,
        16,
        button_width,
        button_height,
        window_,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(TrayCommand::kExit)),
        hinstance_,
        nullptr);
}

LRESULT CALLBACK DesktopOverlayController::OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    DesktopOverlayController* controller = GetController(hwnd);
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
