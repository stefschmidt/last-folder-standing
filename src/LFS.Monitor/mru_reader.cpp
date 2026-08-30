#include "mru_reader.h"

#include <windows.h>
#include <shlobj.h>

#include <cstring>
#include <string>

#include "common/timeutil.h"

namespace lfs {
namespace {

// A PIDL longer than this is not something a file dialog wrote.
constexpr size_t kMaxPidlBytes = 64 * 1024;
constexpr int kMaxPidlItems = 512;

struct RegKey {
    HKEY h = nullptr;
    ~RegKey() {
        if (h) ::RegCloseKey(h);
    }
    RegKey() = default;
    RegKey(const RegKey&) = delete;
    RegKey& operator=(const RegKey&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

bool OpenSubKey(HKEY parent, const wchar_t* sub, RegKey& out) {
    return ::RegOpenKeyExW(parent, sub, 0, KEY_READ, &out.h) == ERROR_SUCCESS;
}

uint64_t KeyLastWrite(HKEY key) {
    FILETIME ft{};
    if (::RegQueryInfoKeyW(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                           nullptr, nullptr, nullptr, &ft) != ERROR_SUCCESS) {
        return 0;
    }
    return FileTimeToU64(ft);
}

bool ReadBinaryValue(HKEY key, const wchar_t* name, std::vector<BYTE>& out) {
    out.clear();
    DWORD type = 0;
    DWORD size = 0;
    if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS) return false;
    if (type != REG_BINARY || size == 0 || size > kMaxPidlBytes) return false;
    out.resize(size);
    if (::RegQueryValueExW(key, name, nullptr, &type, out.data(), &size) != ERROR_SUCCESS) {
        out.clear();
        return false;
    }
    out.resize(size);
    return true;
}

// MRUListEx is a sequence of little-endian DWORDs, most recent first,
// terminated by 0xFFFFFFFF.
std::vector<DWORD> ReadMruOrder(HKEY key) {
    std::vector<BYTE> blob;
    if (!ReadBinaryValue(key, L"MRUListEx", blob)) return {};

    std::vector<DWORD> order;
    for (size_t off = 0; off + sizeof(DWORD) <= blob.size(); off += sizeof(DWORD)) {
        DWORD v = 0;
        std::memcpy(&v, blob.data() + off, sizeof(v));
        if (v == 0xFFFFFFFFu) break;
        order.push_back(v);
        if (order.size() > 1024) break;  // corrupt list, stop
    }
    return order;
}

// Walks the SHITEMID chain and verifies it stays inside the blob.
// A PIDL we hand to the shell must be well-formed or the shell will fault.
bool IsPlausiblePidl(const BYTE* data, size_t size) {
    size_t off = 0;
    int items = 0;
    for (;;) {
        if (off + sizeof(USHORT) > size) return false;
        USHORT cb = 0;
        std::memcpy(&cb, data + off, sizeof(cb));
        if (cb == 0) return items > 0;  // terminator; empty PIDL = desktop, not useful
        if (cb < sizeof(USHORT) + 1) return false;
        if (off + cb > size) return false;
        off += cb;
        if (++items > kMaxPidlItems) return false;
    }
}

// Copies the blob (ILRemoveLastID writes in place), optionally drops the last
// item to get the parent folder, then asks the shell for a filesystem path.
// Virtual locations (Libraries, search results, MTP) yield nothing -> skipped.
std::wstring PathFromPidlBlob(const BYTE* data, size_t size, bool stripLastId) {
    if (!IsPlausiblePidl(data, size)) return {};

    std::vector<BYTE> buf(data, data + size);
    auto* pidl = reinterpret_cast<LPITEMIDLIST>(buf.data());

    if (stripLastId && !::ILRemoveLastID(pidl)) return {};
    // After stripping, an empty PIDL means the file sat on the desktop root.
    if (buf.size() < 2) return {};

    std::vector<wchar_t> path(32768, L'\0');
    if (!::SHGetPathFromIDListEx(pidl, path.data(), static_cast<DWORD>(path.size()),
                                 GPFIDL_DEFAULT)) {
        return {};
    }
    return std::wstring(path.data());
}

// Reads one MRU key: values named "0", "1", ... plus MRUListEx for the order.
// `decode` turns a value blob into a folder path.
template <typename Decoder>
void ReadMruKey(HKEY key, const std::wstring& groupName, Source source, Decoder decode,
                std::vector<RawEntry>& out) {
    const std::vector<DWORD> order = ReadMruOrder(key);
    if (order.empty()) return;

    const uint64_t keyTime = KeyLastWrite(key);

    int rank = 0;
    for (const DWORD index : order) {
        const std::wstring valueName = std::to_wstring(index);
        std::vector<BYTE> blob;
        if (!ReadBinaryValue(key, valueName.c_str(), blob)) continue;

        std::wstring path = decode(blob);
        if (path.empty()) continue;

        RawEntry e;
        e.path = std::move(path);
        e.source = source;
        e.rank = rank;
        e.isHead = (rank == 0);
        // Only the head entry is provably "used at keyTime". Older ranks get a
        // slightly older stamp so a first run still sorts sensibly; the snapshot
        // takes over from the second run on.
        e.observedUtc = keyTime > static_cast<uint64_t>(rank) * kSecond
                            ? keyTime - static_cast<uint64_t>(rank) * kSecond
                            : keyTime;
        e.group = groupName;
        out.push_back(std::move(e));
        ++rank;
    }
}

}  // namespace

std::vector<RawEntry> ReadOpenSaveMru() {
    std::vector<RawEntry> out;

    RegKey comdlg;
    if (!OpenSubKey(HKEY_CURRENT_USER, kComDlg32Key, comdlg)) return out;

    RegKey root;
    if (!OpenSubKey(comdlg.h, L"OpenSavePidlMRU", root)) return out;

    for (DWORD i = 0;; ++i) {
        wchar_t name[256];
        DWORD nameLen = ARRAYSIZE(name);
        const LONG rc = ::RegEnumKeyExW(root.h, i, name, &nameLen, nullptr, nullptr, nullptr,
                                        nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue;

        RegKey ext;
        if (!OpenSubKey(root.h, name, ext)) continue;

        ReadMruKey(
            ext.h, name, Source::OpenSave,
            [](const std::vector<BYTE>& blob) {
                return PathFromPidlBlob(blob.data(), blob.size(), /*stripLastId=*/true);
            },
            out);
    }

    return out;
}

std::vector<RawEntry> ReadLastVisitedMru() {
    std::vector<RawEntry> out;

    RegKey comdlg;
    if (!OpenSubKey(HKEY_CURRENT_USER, kComDlg32Key, comdlg)) return out;

    // Windows 10/11 write the "Legacy" key as well; read whatever exists.
    for (const wchar_t* keyName : {L"LastVisitedPidlMRU", L"LastVisitedPidlMRULegacy"}) {
        RegKey key;
        if (!OpenSubKey(comdlg.h, keyName, key)) continue;

        ReadMruKey(
            key.h, keyName, Source::LastVisited,
            [](const std::vector<BYTE>& blob) -> std::wstring {
                // UTF-16 executable name, NUL-terminated, then the folder PIDL.
                const size_t wchars = blob.size() / sizeof(wchar_t);
                const auto* text = reinterpret_cast<const wchar_t*>(blob.data());
                size_t nameLen = 0;
                while (nameLen < wchars && text[nameLen] != L'\0') ++nameLen;
                if (nameLen == wchars) return {};  // no terminator -> garbage

                const size_t offset = (nameLen + 1) * sizeof(wchar_t);
                if (offset >= blob.size()) return {};
                // The PIDL here already points at the folder.
                return PathFromPidlBlob(blob.data() + offset, blob.size() - offset,
                                        /*stripLastId=*/false);
            },
            out);
    }

    return out;
}

}  // namespace lfs
