#pragma once

#ifdef _WIN32

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <wrl.h>
#include <WebView2.h>

namespace screamrouter::desktop {

/**
 * DesktopOverlayController hosts the transparent WebView2 window that renders the
 * DesktopMenu route selection UI. It also owns the notification area icon that
 * toggles the overlay visibility.
 */
class DesktopOverlayController {
public:
    DesktopOverlayController();
    ~DesktopOverlayController();

    DesktopOverlayController(const DesktopOverlayController&) = delete;
    DesktopOverlayController& operator=(const DesktopOverlayController&) = delete;

    bool Start(const std::wstring& url, int width, int height);
    void Show();
    void Hide();
    void Toggle();
    void Shutdown();

private:
    enum class ControlCommand : WPARAM {
        kShow = 1,
        kHide = 2,
        kToggle = 3,
        kShutdown = 4,
    };

    enum class TrayCommand : UINT {
        kToggle = 1,
        kExit = 2,
    };

    static constexpr UINT kTrayCallbackMessage = WM_APP + 1;  // Use WM_APP range for app messages
    static constexpr UINT kControlMessage = WM_APP + 2;
    static constexpr UINT kTrayIconId = 1;  // Must be <= 0xFFFF for V4
    static constexpr UINT_PTR kMouseTimerId = 1001;
    static constexpr UINT_PTR kColorTimerId = 1002;
    static constexpr int kDefaultWidth = 900;
    static constexpr int kDefaultHeight = 600;

    static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    void UiThreadMain(std::wstring url, int width, int height);
    void CleanupTrayIcon();
    void EnsureTrayIcon();
    void BuildTrayMenu();
    void ShowTrayMenu(const POINT& anchor);
    void InitWebView();
    void InjectHelpers();
    void Navigate();
    void SendDesktopMenuShow();
    void SendDesktopMenuHide();
    void RefreshAccentColor();
    void DisableMouse();
    void EnableMouse();
    void HandleMouseTimer();
    void HandleColorTimer();
    void HandleTrayEvent(WPARAM wparam, LPARAM lparam);
    void HandleCommand(WPARAM wparam);
    void UpdateWebViewBounds();
    void FocusWebView();

    std::thread ui_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> ready_{false};
    std::wstring url_;
    int width_{kDefaultWidth};
    int height_{kDefaultHeight};

    HWND window_{nullptr};
    HMENU tray_menu_{nullptr};
    NOTIFYICONDATAW nid_{};
    HINSTANCE hinstance_{nullptr};
    HMODULE resource_module_{nullptr};
    HICON tray_icon_{nullptr};

    bool mouse_disabled_{false};
    POINT last_mouse_{};
    bool script_pending_{false};
    COLORREF accent_color_{RGB(0, 120, 215)};

    Microsoft::WRL::ComPtr<ICoreWebView2Environment> webview_env_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> webview_controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview_;

    std::mutex state_mutex_;
    GUID tray_guid_{0x9C9AA8C2, 0x5A45, 0x4F24, {0x93, 0xB2, 0x0A, 0x64, 0x78, 0xF9, 0x01, 0x72}};

    RECT GetWorkArea() const;
    void PositionWindow();
};

}  // namespace screamrouter::desktop

#endif  // _WIN32
