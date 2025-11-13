#ifdef _WIN32

#include "windows/desktop_overlay/desktop_overlay.h"
#include "windows/resources/resource.h"

#include <Shlwapi.h>
#include <ShlObj.h>
#include <Shobjidl.h>
#include <KnownFolders.h>
#include <Strsafe.h>
#include <comdef.h>
#include <windowsx.h>
#include <propvarutil.h>
#include <propkey.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <iterator>
#include <utility>
#include <vector>

#include "utils/cpp_logger.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "propsys.lib")

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
constexpr wchar_t kPopupHelperScript[] = LR"JS(
    window.close = function() {
        try {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage(JSON.stringify({ action: 'close' }));
                return;
            }
        } catch (e) {
            if (window.chrome && window.chrome.webview) {
                window.chrome.webview.postMessage('{\"action\":\"close\"}');
                return;
            }
        }
        window.open('', '_self', '');
    };
)JS";
constexpr UINT_PTR kPopupMouseTimerId = 2001;
constexpr UINT kPopupTimerIntervalMs = 50;
constexpr int kTranscriptionHeight = 600;
constexpr int kTranscriptionBottomMargin = 100;
constexpr double kTranscriptionWidthPercent = 0.8;

static COLORREF ExtractColor(ICoreWebView2Environment* /*env*/) {
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&color, &opaque))) {
        // DwmGetColorizationColor returns 0xAARRGGBB
        // Convert to COLORREF which is 0x00BBGGRR
        BYTE r = (color >> 16) & 0xFF;
        BYTE g = (color >> 8) & 0xFF;
        BYTE b = color & 0xFF;
        return RGB(r, g, b);
    }
    return RGB(0, 120, 215);
}

DesktopOverlayController* GetController(HWND hwnd) {
    return reinterpret_cast<DesktopOverlayController*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

}  // namespace

DesktopOverlayController::DesktopOverlayController() {
    ZeroMemory(&nid_, sizeof(nid_));

    const auto module_address =
        reinterpret_cast<LPCWSTR>(&DesktopOverlayController::OverlayWndProc);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            module_address, &resource_module_)) {
        resource_module_ = nullptr;
        LOG_CPP_WARNING("GetModuleHandleExW failed for resource module (err=%lu)", GetLastError());
    }

    auto load_icon_from = [](HMODULE module) -> HICON {
        if (!module) {
            return nullptr;
        }
        return LoadIconW(module, MAKEINTRESOURCEW(IDI_SCREAMROUTER_ICON));
    };

    tray_icon_ = load_icon_from(resource_module_);
    if (tray_icon_) {
        LOG_CPP_INFO("Loaded tray icon from screamrouter_audio_engine module");
    }

    if (!tray_icon_) {
        HINSTANCE host_instance = GetModuleHandle(nullptr);
        tray_icon_ = load_icon_from(host_instance);
        if (tray_icon_) {
            LOG_CPP_INFO("Loaded tray icon from host executable");
        }
    }

    if (!tray_icon_) {
        // Try LoadImage which might work better for resources
        auto load_image_from = [](HMODULE module) -> HICON {
            if (!module) {
                return nullptr;
            }
            return reinterpret_cast<HICON>(LoadImageW(module,
                                                      MAKEINTRESOURCEW(IDI_SCREAMROUTER_ICON),
                                                      IMAGE_ICON,
                                                      0,
                                                      0,
                                                      LR_DEFAULTSIZE | LR_SHARED));
        };
        tray_icon_ = load_image_from(resource_module_);
        if (!tray_icon_) {
            tray_icon_ = load_image_from(GetModuleHandle(nullptr));
        }
        if (tray_icon_) {
            LOG_CPP_INFO("Loaded tray icon using LoadImage fallback");
        }
    }

    // Fallback to default icon
    if (!tray_icon_) {
        LOG_CPP_WARNING("Failed to load ScreamRouter icon from resources (err=%lu), using default", GetLastError());
        tray_icon_ = LoadIconW(nullptr, IDI_APPLICATION);
    }

    InitializeIconResourcePath();
    EnsureProcessAppId();
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
    // Always show, never hide from tray click
    Show();
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
    HICON class_icon = tray_icon_ ? tray_icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIcon = class_icon;
    wc.hIconSm = class_icon;
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
    UpdateWindowAppId(window_);

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

    CleanupPopupWindows();
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

    // Set browser arguments to ignore certificate errors for self-signed certs
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

                    // Add navigation event handlers for debugging
                    webview_->add_NavigationStarting(
                        Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                            [this](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                LPWSTR uri = nullptr;
                                args->get_Uri(&uri);
                                if (uri) {
                                    LOG_CPP_INFO("DesktopOverlay navigation starting: %ls", uri);
                                    CoTaskMemFree(uri);
                                }
                                return S_OK;
                            }).Get(),
                        nullptr);

                    webview_->add_NavigationCompleted(
                        Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                            [this](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                BOOL success = FALSE;
                                args->get_IsSuccess(&success);
                                if (success) {
                                    LOG_CPP_INFO("DesktopOverlay navigation completed successfully");
                                } else {
                                    COREWEBVIEW2_WEB_ERROR_STATUS error_status;
                                    args->get_WebErrorStatus(&error_status);
                                    LOG_CPP_ERROR("DesktopOverlay navigation failed with error status: %d", static_cast<int>(error_status));
                                }
                                return S_OK;
                            }).Get(),
                        nullptr);

                    webview_->add_NewWindowRequested(
                        Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                            [this](ICoreWebView2* /*sender*/, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                                HandleNewWindowRequested(args);
                                return S_OK;
                            }).Get(),
                        nullptr);

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
        nullptr,  // No options object needed when using environment variable
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
        LOG_CPP_INFO("DesktopOverlay navigating to URL: %ls", url_.c_str());
        HRESULT hr = webview_->Navigate(url_.c_str());
        if (FAILED(hr)) {
            LOG_CPP_ERROR("DesktopOverlay Navigate() failed with hr=0x%08X", hr);
        }
    } else {
        LOG_CPP_WARNING("DesktopOverlay Navigate() called but webview_ is %p and url_ is %s",
                        webview_.Get(), url_.empty() ? "empty" : "not empty");
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
        // DwmGetColorizationColor returns 0xAARRGGBB
        // We need to convert to COLORREF which is 0x00BBGGRR
        BYTE a = (color >> 24) & 0xFF;
        BYTE r = (color >> 16) & 0xFF;
        BYTE g = (color >> 8) & 0xFF;
        BYTE b = color & 0xFF;
        accent_color_ = RGB(r, g, b);  // RGB macro creates 0x00BBGGRR
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

    // First, remove any existing icon with this GUID
    NOTIFYICONDATAW remove{};
    remove.cbSize = sizeof(remove);
    remove.hWnd = window_;
    remove.uID = kTrayIconId;
    remove.guidItem = tray_guid_;
    remove.uFlags = NIF_GUID;
    Shell_NotifyIconW(NIM_DELETE, &remove);

    // Add the icon with all required flags
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = window_;
    nid.uID = kTrayIconId;
    nid.guidItem = tray_guid_;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = tray_icon_ ? tray_icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), kTrayTooltip);

    // Required flags for V4
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_GUID | NIF_SHOWTIP;

    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        LOG_CPP_ERROR("DesktopOverlay failed to add tray icon (err=%lu)", GetLastError());
        return;
    }

    // CRITICAL: Must call NIM_SETVERSION immediately after NIM_ADD for V4 semantics
    nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &nid)) {
        LOG_CPP_WARNING("Failed to set tray icon version to V4 (err=%lu)", GetLastError());
    }

    nid_ = nid;
    LOG_CPP_INFO("Tray icon added with V4 semantics");
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
    AppendMenuW(tray_menu_, MF_STRING, static_cast<UINT>(TrayCommand::kToggle), L"Show Desktop Menu");
    AppendMenuW(tray_menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tray_menu_, MF_STRING, static_cast<UINT>(TrayCommand::kExit), L"Exit ScreamRouter");
}

void DesktopOverlayController::ShowTrayMenu(const POINT& anchor) {
    if (!tray_menu_) {
        BuildTrayMenu();
    }

    // Use the anchor coordinates from V4 message (works in overflow too)
    POINT pt = anchor;

    // REQUIRED: Set foreground window before TrackPopupMenu so menu dismisses properly
    SetForegroundWindow(window_);

    // Show the menu at the anchor point
    TrackPopupMenuEx(tray_menu_, TPM_RIGHTBUTTON, pt.x, pt.y, window_, nullptr);

    // Optional: Return focus to tray after menu closes
    NOTIFYICONDATAW focus{};
    focus.cbSize = sizeof(focus);
    focus.hWnd = window_;
    focus.uID = kTrayIconId;
    focus.guidItem = tray_guid_;
    focus.uFlags = NIF_GUID;
    Shell_NotifyIconW(NIM_SETFOCUS, &focus);
}

void DesktopOverlayController::HandleTrayEvent(WPARAM wparam, LPARAM lparam) {
    // V4 semantics: LOWORD(lparam) = event, HIWORD(lparam) = icon ID
    const UINT event = LOWORD(lparam);
    const UINT icon_id = HIWORD(lparam);

    if (icon_id != kTrayIconId) {
        LOG_CPP_DEBUG("DesktopOverlay tray event for different icon (%u)", icon_id);
        return;
    }

    // V4 semantics: wParam contains anchor coordinates for mouse/keyboard events
    POINT anchor{
        GET_X_LPARAM(wparam),
        GET_Y_LPARAM(wparam)
    };

    switch (event) {
    case WM_LBUTTONUP:  // Single left click
        LOG_CPP_INFO("DesktopOverlay tray left-click");
        Toggle();
        break;

    case WM_LBUTTONDBLCLK:  // Double left click
        LOG_CPP_INFO("DesktopOverlay tray double-click");
        Toggle();
        break;

    case NIN_SELECT:  // Keyboard Enter after selection
    case NIN_KEYSELECT:  // Keyboard Space/Enter
        LOG_CPP_INFO("DesktopOverlay tray keyboard activation (event=0x%04x)", event);
        Toggle();
        break;

    case WM_CONTEXTMENU:  // Keyboard context menu or right-click
    case WM_RBUTTONUP:    // Right mouse button up
        LOG_CPP_INFO("DesktopOverlay tray context menu request");
        ShowTrayMenu(anchor);
        break;

    case NIN_POPUPOPEN:  // Overflow fly-out opened
        LOG_CPP_DEBUG("DesktopOverlay tray popup opened");
        break;

    case NIN_POPUPCLOSE:  // Overflow fly-out closed
        LOG_CPP_DEBUG("DesktopOverlay tray popup closed");
        break;

    default:
        LOG_CPP_DEBUG("DesktopOverlay tray event 0x%04x", event);
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

void DesktopOverlayController::EnsureProcessAppId() {
    if (process_app_id_set_) {
        return;
    }
    if (app_user_model_id_.empty()) {
        app_user_model_id_ = L"ScreamRouter.DesktopOverlay";
    }
    HRESULT hr = SetCurrentProcessExplicitAppUserModelID(app_user_model_id_.c_str());
    if (FAILED(hr)) {
        LOG_CPP_WARNING("DesktopOverlay failed to set process AppUserModelID (hr=0x%08X)", hr);
        return;
    }
    process_app_id_set_ = true;
}

void DesktopOverlayController::InitializeIconResourcePath() {
    if (!icon_resource_path_.empty()) {
        return;
    }
    wchar_t module_path[MAX_PATH]{};
    HMODULE icon_module = resource_module_ ? resource_module_ : GetModuleHandle(nullptr);
    DWORD len = GetModuleFileNameW(icon_module, module_path, ARRAYSIZE(module_path));
    if (len == 0 || len == ARRAYSIZE(module_path)) {
        LOG_CPP_WARNING("DesktopOverlay failed to get module path for icon (err=%lu)", GetLastError());
        return;
    }
    icon_resource_path_ = module_path;
    icon_resource_path_ += L",";
    icon_resource_path_ += std::to_wstring(IDI_SCREAMROUTER_ICON);
}

void DesktopOverlayController::UpdateWindowAppId(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    EnsureProcessAppId();
    InitializeIconResourcePath();

    Microsoft::WRL::ComPtr<IPropertyStore> store;
    HRESULT hr = SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store));
    if (FAILED(hr) || !store) {
        LOG_CPP_WARNING("DesktopOverlay failed to get property store for window (hr=0x%08X)", hr);
        return;
    }

    bool changed = false;
    PROPVARIANT value;
    PropVariantInit(&value);

    if (!app_user_model_id_.empty() &&
        SUCCEEDED(InitPropVariantFromString(app_user_model_id_.c_str(), &value))) {
        if (SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, value))) {
            changed = true;
        }
        PropVariantClear(&value);
    }

    if (!icon_resource_path_.empty() &&
        SUCCEEDED(InitPropVariantFromString(icon_resource_path_.c_str(), &value))) {
        if (SUCCEEDED(store->SetValue(PKEY_AppUserModel_RelaunchIconResource, value))) {
            changed = true;
        }
        PropVariantClear(&value);
    }

    if (changed) {
        store->Commit();
    }
}

void DesktopOverlayController::HandleNewWindowRequested(ICoreWebView2NewWindowRequestedEventArgs* args) {
    if (!args) {
        return;
    }

    args->put_Handled(TRUE);

    std::wstring uri;
    std::wstring name;

    LPWSTR raw_uri = nullptr;
    if (SUCCEEDED(args->get_Uri(&raw_uri)) && raw_uri) {
        uri.assign(raw_uri);
        CoTaskMemFree(raw_uri);
    }
    Microsoft::WRL::ComPtr<ICoreWebView2NewWindowRequestedEventArgs2> args2;
    if (SUCCEEDED(args->QueryInterface(IID_PPV_ARGS(&args2))) && args2) {
        LPWSTR raw_name = nullptr;
        if (SUCCEEDED(args2->get_Name(&raw_name)) && raw_name) {
            name.assign(raw_name);
            CoTaskMemFree(raw_name);
        }
    }

    bool is_transcription = (_wcsicmp(name.c_str(), L"Transcription") == 0);

    auto popup = std::make_unique<PopupWindow>();
    popup->owner = this;
    popup->initial_uri = uri;
    popup->name = name.empty() ? L"ScreamRouter Popup" : name;
    popup->is_transcription = is_transcription;

    Microsoft::WRL::ComPtr<ICoreWebView2WindowFeatures> window_features;
    if (SUCCEEDED(args->get_WindowFeatures(&window_features)) && window_features) {
        BOOL has_size = FALSE;
        if (SUCCEEDED(window_features->get_HasSize(&has_size)) && has_size) {
            UINT32 requested_width = 0;
            UINT32 requested_height = 0;
            if (SUCCEEDED(window_features->get_Width(&requested_width)) &&
                SUCCEEDED(window_features->get_Height(&requested_height)) &&
                requested_width > 0 && requested_height > 0) {
                popup->has_requested_size = true;
                popup->requested_width = static_cast<int>(requested_width);
                popup->requested_height = static_cast<int>(requested_height);
            }
        }

        BOOL has_position = FALSE;
        if (SUCCEEDED(window_features->get_HasPosition(&has_position)) && has_position) {
            UINT32 requested_left = 0;
            UINT32 requested_top = 0;
            if (SUCCEEDED(window_features->get_Left(&requested_left)) &&
                SUCCEEDED(window_features->get_Top(&requested_top))) {
                popup->has_requested_position = true;
                popup->requested_left = static_cast<int>(requested_left);
                popup->requested_top = static_cast<int>(requested_top);
            }
        }
    }

    if (!CreatePopupWindow(*popup)) {
        LOG_CPP_ERROR("DesktopOverlay failed to create popup window for '%ls'", popup->name.c_str());
        return;
    }

    HWND hwnd = popup->hwnd;
    auto inserted = popup_windows_.emplace(hwnd, std::move(popup));
    PopupWindow& popup_ref = *inserted.first->second;
    StartPopupMouseTimer(popup_ref);
    InitPopupWebView(hwnd);
}

bool DesktopOverlayController::RegisterPopupWindowClass() {
    if (popup_class_atom_) {
        return true;
    }
    if (!hinstance_) {
        hinstance_ = GetModuleHandle(nullptr);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = PopupWndProc;
    wc.hInstance = hinstance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = popup_class_name_.c_str();

    popup_class_atom_ = RegisterClassExW(&wc);
    if (!popup_class_atom_) {
        LOG_CPP_ERROR("DesktopOverlay failed to register popup class (err=%lu)", GetLastError());
        return false;
    }
    return true;
}

bool DesktopOverlayController::CreatePopupWindow(PopupWindow& popup) {
    if (!RegisterPopupWindowClass()) {
        return false;
    }

    RECT work = GetWorkArea();
    const int work_left = static_cast<int>(work.left);
    const int work_top = static_cast<int>(work.top);
    const int work_right = static_cast<int>(work.right);
    const int work_bottom = static_cast<int>(work.bottom);
    const int work_w = work_right - work_left;
    const int work_h = work_bottom - work_top;

    int width = 1366;
    int height = 768;
    int left = work_left + (work_w - width) / 2;
    int top = work_top + (work_h - height) / 2;

    DWORD style = WS_OVERLAPPEDWINDOW;
    DWORD ex_style = WS_EX_APPWINDOW;

    if (popup.is_transcription) {
        width = static_cast<int>(work_w * kTranscriptionWidthPercent);
        width = std::max(width, 640);
        width = std::min(width, work_w);
        height = kTranscriptionHeight;
        left = work_left + static_cast<int>(work_w * (1.0 - kTranscriptionWidthPercent) / 2.0);
        top = work_bottom - height - kTranscriptionBottomMargin;
        top = std::max(top, work_top);
        style = WS_POPUP;
        ex_style = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED;
    }

    if (popup.has_requested_size) {
        width = popup.requested_width;
        height = popup.requested_height;
    }
    width = std::max(width, 100);
    height = std::max(height, 100);
    width = std::min(width, work_w);
    height = std::min(height, work_h);

    if (popup.has_requested_position) {
        left = popup.requested_left;
        top = popup.requested_top;
    }

    left = std::max(left, work_left);
    top = std::max(top, work_top);
    if (left + width > work_right) {
        left = std::max(work_right - width, work_left);
    }
    if (top + height > work_bottom) {
        top = std::max(work_bottom - height, work_top);
    }

    HWND hwnd = CreateWindowExW(
        ex_style,
        popup_class_name_.c_str(),
        popup.name.c_str(),
        style,
        left,
        top,
        width,
        height,
        nullptr,
        nullptr,
        hinstance_,
        &popup);

    if (!hwnd) {
        LOG_CPP_ERROR("DesktopOverlay failed to create popup window hwnd (err=%lu)", GetLastError());
        return false;
    }

    popup.hwnd = hwnd;
    if (tray_icon_) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(tray_icon_));
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(tray_icon_));
    }
    UpdateWindowAppId(hwnd);
    popup.mouse_disabled = false;
    popup.script_pending = false;
    popup.original_style = GetWindowLong(hwnd, GWL_STYLE);
    popup.original_ex_style = GetWindowLong(hwnd, GWL_EXSTYLE);
    popup.original_placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &popup.original_placement);

    if (popup.is_transcription) {
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 255, LWA_COLORKEY | LWA_ALPHA);
        SetWindowPos(hwnd, HWND_TOPMOST, left, top, width, height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    } else {
        ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);
    }

    return true;
}

void DesktopOverlayController::InitPopupWebView(HWND hwnd) {
    if (!webview_env_) {
        LOG_CPP_WARNING("DesktopOverlay cannot initialize popup WebView2 without environment");
        return;
    }
    auto it = popup_windows_.find(hwnd);
    if (it == popup_windows_.end()) {
        return;
    }

    PopupWindow* popup = it->second.get();
    webview_env_->CreateCoreWebView2Controller(
        hwnd,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, hwnd](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                auto it = popup_windows_.find(hwnd);
                if (it == popup_windows_.end()) {
                    if (controller) {
                        controller->Close();
                    }
                    return S_OK;
                }
                PopupWindow& popup = *it->second;
                if (FAILED(result) || !controller) {
                    LOG_CPP_ERROR("DesktopOverlay popup controller creation failed (hr=0x%08X)", result);
                    DestroyWindow(hwnd);
                    return result;
                }
                popup.controller = controller;
                controller->get_CoreWebView2(&popup.webview);
                ConfigurePopupWebView(popup);
                if (!popup.initial_uri.empty()) {
                    popup.webview->Navigate(popup.initial_uri.c_str());
                }
                return S_OK;
            })
            .Get());
}

void DesktopOverlayController::ConfigurePopupWebView(PopupWindow& popup) {
    if (!popup.controller || !popup.webview) {
        return;
    }

    UpdatePopupWebViewBounds(popup);
    popup.controller->put_IsVisible(TRUE);

    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
    if (SUCCEEDED(popup.controller.As(&controller2)) && controller2) {
        COREWEBVIEW2_COLOR color{};
        color.A = popup.is_transcription ? 0 : 255;
        color.R = 0;
        color.G = 0;
        color.B = 0;
        controller2->put_DefaultBackgroundColor(color);
    }

    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(popup.webview->get_Settings(&settings)) && settings) {
        settings->put_IsStatusBarEnabled(FALSE);
        settings->put_AreDefaultContextMenusEnabled(TRUE);
        settings->put_AreDevToolsEnabled(TRUE);
        settings->put_IsBuiltInErrorPageEnabled(FALSE);
        settings->put_IsZoomControlEnabled(FALSE);
        settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    }

    popup.webview->AddScriptToExecuteOnDocumentCreated(kJsHelper, nullptr);
    popup.webview->AddScriptToExecuteOnDocumentCreated(kPopupHelperScript, nullptr);

    popup.webview->add_WebMessageReceived(
        Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [hwnd = popup.hwnd](ICoreWebView2* /*sender*/, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR json = nullptr;
                if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {
                    if (wcsstr(json, L"\"action\":\"close\"")) {
                        PostMessage(hwnd, WM_CLOSE, 0, 0);
                    }
                    CoTaskMemFree(json);
                }
                return S_OK;
            })
            .Get(),
        nullptr);

    popup.webview->add_NewWindowRequested(
        Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [this](ICoreWebView2* /*sender*/, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                HandleNewWindowRequested(args);
                return S_OK;
            })
            .Get(),
        nullptr);

    if (!popup.is_transcription) {
        popup.webview->add_DocumentTitleChanged(
            Microsoft::WRL::Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                [hwnd = popup.hwnd](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
                    LPWSTR title = nullptr;
                    if (SUCCEEDED(sender->get_DocumentTitle(&title)) && title) {
                        SetWindowTextW(hwnd, title);
                        CoTaskMemFree(title);
                    }
                    return S_OK;
                })
                .Get(),
            nullptr);

        popup.webview->add_ContainsFullScreenElementChanged(
            Microsoft::WRL::Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                [this, hwnd = popup.hwnd](ICoreWebView2* sender, IUnknown* /*args*/) -> HRESULT {
                    BOOL fullscreen = FALSE;
                    sender->get_ContainsFullScreenElement(&fullscreen);
                    auto it = popup_windows_.find(hwnd);
                    if (it != popup_windows_.end()) {
                        TogglePopupFullscreen(*it->second, fullscreen == TRUE);
                    }
                    return S_OK;
                })
                .Get(),
            nullptr);
    }
}

void DesktopOverlayController::StartPopupMouseTimer(PopupWindow& popup) {
    if (!popup.is_transcription || popup.timer_id || !popup.hwnd) {
        return;
    }
    popup.timer_id = SetTimer(popup.hwnd, kPopupMouseTimerId, kPopupTimerIntervalMs, nullptr);
}

void DesktopOverlayController::HandlePopupMouseTimer(PopupWindow& popup) {
    if (!popup.is_transcription || !popup.webview || popup.script_pending) {
        return;
    }

    POINT pt{};
    GetCursorPos(&pt);
    if (pt.x == popup.last_mouse.x && pt.y == popup.last_mouse.y) {
        return;
    }
    popup.last_mouse = pt;

    RECT rect{};
    GetWindowRect(popup.hwnd, &rect);
    if (!PtInRect(&rect, pt)) {
        return;
    }

    POINT client_pt = pt;
    ScreenToClient(popup.hwnd, &client_pt);
    UINT dpi = GetDpiForWindow(popup.hwnd);
    const float scale = dpi / 96.0f;
    const int scaled_x = static_cast<int>(client_pt.x / scale);
    const int scaled_y = static_cast<int>(client_pt.y / scale);

    std::wstring script = L"(function(){return isPointOverBody(" +
                          std::to_wstring(scaled_x) + L"," + std::to_wstring(scaled_y) +
                          L");})()";

    popup.script_pending = true;
    popup.webview->ExecuteScript(
        script.c_str(),
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [this, hwnd = popup.hwnd](HRESULT error, PCWSTR result) -> HRESULT {
                auto it = popup_windows_.find(hwnd);
                if (it == popup_windows_.end()) {
                    return S_OK;
                }
                PopupWindow& popup = *it->second;
                popup.script_pending = false;
                if (FAILED(error) || !result) {
                    LOG_CPP_WARNING("DesktopOverlay popup hit-test script failed (hr=0x%08X)", error);
                    return S_OK;
                }
                bool over_body = (wcscmp(result, L"true") == 0) || (wcscmp(result, L"\"true\"") == 0);
                if (over_body && !popup.mouse_disabled) {
                    DisablePopupMouse(popup);
                } else if (!over_body && popup.mouse_disabled) {
                    EnablePopupMouse(popup);
                }
                return S_OK;
            })
            .Get());
}

void DesktopOverlayController::DisablePopupMouse(PopupWindow& popup) {
    if (!popup.hwnd || popup.mouse_disabled) {
        return;
    }
    LONG style = GetWindowLong(popup.hwnd, GWL_EXSTYLE);
    style |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    SetWindowLong(popup.hwnd, GWL_EXSTYLE, style);
    SetLayeredWindowAttributes(popup.hwnd, 0, 255, LWA_ALPHA);
    SetWindowPos(popup.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    popup.mouse_disabled = true;
}

void DesktopOverlayController::EnablePopupMouse(PopupWindow& popup) {
    if (!popup.hwnd || !popup.mouse_disabled) {
        return;
    }
    LONG style = GetWindowLong(popup.hwnd, GWL_EXSTYLE);
    style &= ~WS_EX_TRANSPARENT;
    if (style & WS_EX_LAYERED) {
        style &= ~WS_EX_LAYERED;
    }
    SetWindowLong(popup.hwnd, GWL_EXSTYLE, style);
    SetLayeredWindowAttributes(popup.hwnd, 0, 255, LWA_ALPHA);
    SetWindowPos(popup.hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    popup.mouse_disabled = false;
}

void DesktopOverlayController::UpdatePopupWebViewBounds(PopupWindow& popup) {
    if (!popup.hwnd || !popup.controller) {
        return;
    }
    RECT bounds{};
    GetClientRect(popup.hwnd, &bounds);
    popup.controller->put_Bounds(bounds);
}

void DesktopOverlayController::TogglePopupFullscreen(PopupWindow& popup, bool enable) {
    if (!popup.hwnd || popup.is_transcription || popup.in_fullscreen == enable) {
        return;
    }

    if (enable) {
        popup.original_style = GetWindowLong(popup.hwnd, GWL_STYLE);
        popup.original_ex_style = GetWindowLong(popup.hwnd, GWL_EXSTYLE);
        popup.original_placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(popup.hwnd, &popup.original_placement);

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        GetMonitorInfoW(MonitorFromWindow(popup.hwnd, MONITOR_DEFAULTTONEAREST), &monitor_info);

        SetWindowLong(popup.hwnd, GWL_STYLE, popup.original_style & ~(WS_CAPTION | WS_THICKFRAME));
        SetWindowLong(popup.hwnd, GWL_EXSTYLE,
                      popup.original_ex_style & ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                                  WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
        SetWindowPos(
            popup.hwnd,
            HWND_TOP,
            monitor_info.rcMonitor.left,
            monitor_info.rcMonitor.top,
            monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
            monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLong(popup.hwnd, GWL_STYLE, popup.original_style);
        SetWindowLong(popup.hwnd, GWL_EXSTYLE, popup.original_ex_style);
        if (popup.original_placement.length == sizeof(WINDOWPLACEMENT)) {
            SetWindowPlacement(popup.hwnd, &popup.original_placement);
        }
        SetWindowPos(popup.hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }

    popup.in_fullscreen = enable;
}

void DesktopOverlayController::OnPopupDestroyed(HWND hwnd) {
    auto it = popup_windows_.find(hwnd);
    if (it == popup_windows_.end()) {
        return;
    }
    PopupWindow& popup = *it->second;
    if (popup.timer_id) {
        KillTimer(hwnd, popup.timer_id);
        popup.timer_id = 0;
    }
    if (popup.controller) {
        popup.controller->Close();
        popup.controller.Reset();
    }
    popup.webview.Reset();
    popup_windows_.erase(it);
}

void DesktopOverlayController::CleanupPopupWindows() {
    if (popup_windows_.empty()) {
        return;
    }
    std::vector<HWND> handles;
    handles.reserve(popup_windows_.size());
    for (const auto& entry : popup_windows_) {
        handles.push_back(entry.first);
    }
    for (HWND hwnd : handles) {
        if (IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        } else {
            OnPopupDestroyed(hwnd);
        }
    }
}

LRESULT CALLBACK DesktopOverlayController::PopupWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* popup = reinterpret_cast<PopupWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<LPCREATESTRUCT>(lparam);
        popup = static_cast<PopupWindow*>(create->lpCreateParams);
        if (popup) {
            popup->hwnd = hwnd;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(popup));
        }
        return TRUE;
    }
    case WM_SIZE:
        if (popup && popup->owner) {
            popup->owner->UpdatePopupWebViewBounds(*popup);
        }
        return 0;
    case WM_TIMER:
        if (popup && popup->owner && popup->is_transcription && wparam == kPopupMouseTimerId) {
            popup->owner->HandlePopupMouseTimer(*popup);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (popup && popup->owner) {
            popup->owner->OnPopupDestroyed(hwnd);
        }
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
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
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
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
        // Always show, don't toggle
        if (!IsWindowVisible(window_)) {
            PositionWindow();

            // Force mouse to be enabled initially (interactive)
            mouse_disabled_ = true;  // Set to true so EnableMouse will work
            EnableMouse();

            ShowWindow(window_, SW_SHOW);
            SetForegroundWindow(window_);
            SetFocus(window_);
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

                    ShowWindow(hwnd, SW_SHOW);
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

                    ShowWindow(hwnd, SW_SHOW);
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
