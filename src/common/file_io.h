// File and known-folder access shared by Monitor and ShellExtension.
//
// The shell extension runs inside explorer.exe and every file-dialog host, so
// nothing here throws, blocks, or allocates more than the file it is asked for.
#pragma once

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include <string>

namespace lfs {

// 4 MB. A state/settings file larger than this is corrupt, not data.
inline constexpr LONGLONG kMaxReadableFileBytes = 4 * 1024 * 1024;

inline std::wstring KnownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw))) return {};
    std::wstring result(raw);
    ::CoTaskMemFree(raw);
    return result;
}

// %LOCALAPPDATA%\LastFolderStanding. Does not create anything.
inline std::wstring DataDirPath() {
    const std::wstring local = KnownFolderPath(FOLDERID_LocalAppData);
    if (local.empty()) return {};
    return local + L"\\LastFolderStanding";
}

inline std::wstring StateFilePath() {
    const std::wstring dir = DataDirPath();
    return dir.empty() ? std::wstring{} : dir + L"\\state.json";
}

inline std::wstring SettingsFilePath() {
    const std::wstring dir = DataDirPath();
    return dir.empty() ? std::wstring{} : dir + L"\\settings.json";
}

// Returns the input unchanged if expansion fails, so a broken variable shows up
// as a literal instead of an empty string.
inline std::wstring ExpandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    const DWORD needed = ::ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (needed == 0) return s;
    std::wstring out(needed, L'\0');
    const DWORD written = ::ExpandEnvironmentStringsW(s.c_str(), out.data(), needed);
    if (written == 0 || written > needed) return s;
    out.resize(written - 1);  // drop the terminating NUL
    return out;
}

// Opens with full sharing so a concurrent write by the monitor cannot fail us.
inline bool ReadFileBytes(const std::wstring& path, std::string& out) {
    out.clear();
    if (path.empty()) return false;

    const HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size) || size.QuadPart > kMaxReadableFileBytes) {
        ::CloseHandle(h);
        return false;
    }

    bool ok = true;
    try {
        out.resize(static_cast<size_t>(size.QuadPart));
    } catch (...) {
        ::CloseHandle(h);
        return false;
    }

    size_t offset = 0;
    while (offset < out.size()) {
        DWORD read = 0;
        const size_t remaining = out.size() - offset;
        const DWORD chunk = static_cast<DWORD>(remaining > 0x10000 ? 0x10000 : remaining);
        if (!::ReadFile(h, out.data() + offset, chunk, &read, nullptr) || read == 0) {
            ok = false;
            break;
        }
        offset += read;
    }
    ::CloseHandle(h);

    if (!ok) {
        out.clear();
        return false;
    }

    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

// Writes via temp file + ReplaceFileW: a reader either sees the old file or the
// new one, never a half-written one.
inline bool WriteFileAtomic(const std::wstring& path, const std::string& bytes) {
    if (path.empty()) return false;
    const std::wstring tmp = path + L".tmp";

    const HANDLE h = ::CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        DWORD written = 0;
        const size_t remaining = bytes.size() - offset;
        const DWORD chunk = static_cast<DWORD>(remaining > 0x10000 ? 0x10000 : remaining);
        if (!::WriteFile(h, bytes.data() + offset, chunk, &written, nullptr) || written == 0) {
            ok = false;
            break;
        }
        offset += written;
    }
    if (ok) ok = ::FlushFileBuffers(h) != FALSE;
    ::CloseHandle(h);

    if (!ok) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }

    // ReplaceFileW needs an existing target; the first write is a plain move.
    if (::GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
    } else if (::ReplaceFileW(path.c_str(), tmp.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS,
                              nullptr, nullptr)) {
        return true;
    }

    ::DeleteFileW(tmp.c_str());
    return false;
}

}  // namespace lfs
