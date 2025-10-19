#pragma once

#ifdef _WIN32

#include <string>
#include <windows.h>

namespace screamrouter {
namespace audio {
namespace system_audio {

inline std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr) {
        return {};
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(len - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8.data(), len, nullptr, nullptr);
    return utf8;
}

inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) {
        return std::wstring();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(len - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wide.data(), len);
    return wide;
}

} // namespace system_audio
} // namespace audio
} // namespace screamrouter

#endif // _WIN32

