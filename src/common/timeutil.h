// FILETIME <-> ISO-8601 UTC helpers. Header-only, no exceptions.
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace lfs {

inline uint64_t FileTimeToU64(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

inline FILETIME U64ToFileTime(uint64_t v) {
    ULARGE_INTEGER u;
    u.QuadPart = v;
    FILETIME ft;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    return ft;
}

inline uint64_t NowUtc() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    return FileTimeToU64(ft);
}

constexpr uint64_t kSecond = 10'000'000ULL;  // 100ns ticks

// "2026-08-28T10:15:00Z" -- second resolution is plenty for an MRU list.
inline std::wstring ToIso8601(uint64_t fileTime) {
    const FILETIME ft = U64ToFileTime(fileTime);
    SYSTEMTIME st{};
    if (!::FileTimeToSystemTime(&ft, &st)) return L"1601-01-01T00:00:00Z";
    wchar_t buf[32];
    ::swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour,
                 st.wMinute, st.wSecond);
    return buf;
}

// Tolerant of anything that is not exactly our own output: returns 0 on failure.
inline uint64_t FromIso8601(std::wstring_view s) {
    if (s.size() < 19) return 0;
    const std::wstring text(s);
    unsigned y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (::swscanf_s(text.c_str(), L"%4u-%2u-%2uT%2u:%2u:%2u", &y, &mo, &d, &h, &mi, &sec) != 6) {
        return 0;
    }
    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(y);
    st.wMonth = static_cast<WORD>(mo);
    st.wDay = static_cast<WORD>(d);
    st.wHour = static_cast<WORD>(h);
    st.wMinute = static_cast<WORD>(mi);
    st.wSecond = static_cast<WORD>(sec);
    FILETIME ft{};
    if (!::SystemTimeToFileTime(&st, &ft)) return 0;
    return FileTimeToU64(ft);
}

// Local-time "HH:MM:SS" for --console output.
inline std::wstring ToLocalTimeOfDay(uint64_t fileTime) {
    const FILETIME ft = U64ToFileTime(fileTime);
    FILETIME local{};
    SYSTEMTIME st{};
    if (!::FileTimeToLocalFileTime(&ft, &local) || !::FileTimeToSystemTime(&local, &st)) {
        return L"--:--:--";
    }
    wchar_t buf[16];
    ::swprintf_s(buf, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

}  // namespace lfs
