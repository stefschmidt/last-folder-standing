// Small string helpers shared by Monitor and ShellExtension.
// Header-only, no exceptions thrown, no allocations beyond std::wstring/std::string.
#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace lfs {

inline std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                                             nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

inline std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed,
                          nullptr, nullptr);
    return out;
}

// Ordinal, case-insensitive compare. Good enough for filesystem paths and
// cheaper than a locale-aware compare.
inline bool IEquals(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                  static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

inline bool StartsWith(std::wstring_view s, std::wstring_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

inline bool IStartsWith(std::wstring_view s, std::wstring_view prefix) {
    return s.size() >= prefix.size() && IEquals(s.substr(0, prefix.size()), prefix);
}

inline std::wstring_view TrimWs(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r' ||
                          s.front() == L'\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r' ||
                          s.back() == L'\n')) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace lfs
