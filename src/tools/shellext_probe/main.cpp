// Exercises LFS.ShellExtension without registering it and without involving
// Explorer.
//
// The DLL is loaded directly and driven through the same calls the shell would
// make, including deliberately malformed PIDLs. A crash here is a crash in
// explorer.exe later, so this runs before every registration.
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cstdio>
#include <string>
#include <vector>

#include "common/lfs_guid.h"
#include "pidl.h"

// CMake points this at the DLL from the same build tree. The 32-bit probe tests
// the 32-bit DLL and the 32-bit registry view, which is the only way to see what
// a 32-bit application sees.
#ifndef LFS_EXTENSION_DLL_NAME
#define LFS_EXTENSION_DLL_NAME L"LFS.ShellExtension.dll"
#endif

#if defined(_WIN64)
#define LFS_PROBE_ARCH L"64-bit"
#else
#define LFS_PROBE_ARCH L"32-bit"
#endif

namespace {

int g_failures = 0;

void Check(bool condition, const wchar_t* what) {
    std::wprintf(L"  [%s] %s\n", condition ? L"ok  " : L"FAIL", what);
    if (!condition) ++g_failures;
}

std::wstring StrRetToString(STRRET& str, PCUITEMID_CHILD pidl) {
    wchar_t buffer[MAX_PATH * 4]{};
    if (FAILED(::StrRetToBufW(&str, pidl, buffer, ARRAYSIZE(buffer)))) return L"<error>";
    return buffer;
}

using DllGetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
using DllCanUnloadNowFn = HRESULT(STDAPICALLTYPE*)();

// A PIDL that is not ours, or is ours but broken. Every accessor must reject
// these rather than read past the allocation.
PITEMID_CHILD MakeBogusPidl(USHORT cb, USHORT signature, USHORT version, bool terminatePath) {
    // The buffer always holds a full header, even when cb claims something
    // smaller. The undersized case is about what the item declares to the DLL,
    // not about writing past our own allocation while building it.
    const size_t declared = static_cast<size_t>(cb) + sizeof(USHORT);
    const size_t total = declared > sizeof(lfs::ChildItem) ? declared : sizeof(lfs::ChildItem);
    auto* raw = static_cast<BYTE*>(::CoTaskMemAlloc(total));
    if (!raw) return nullptr;
    ::FillMemory(raw, total, 0x41);  // 'A', so an unterminated path stays unterminated
    auto* item = reinterpret_cast<lfs::ChildItem*>(raw);
    item->cb = cb;
    item->signature = signature;
    item->version = version;
    item->index = 0;
    item->nameStyle = 0;
    if (terminatePath && cb > offsetof(lfs::ChildItem, path) + sizeof(WCHAR)) {
        item->path[0] = L'\0';
    }
    // list terminator
    *reinterpret_cast<USHORT*>(raw + cb) = 0;
    return reinterpret_cast<PITEMID_CHILD>(raw);
}

// Second mode: with the DLL registered, verify the shell really surfaces our
// node where the navigation pane looks for it. This is what "the entry shows up
// in Explorer" reduces to, minus the pixels.
int ProbeRegistered() {
    std::wprintf(L"Registered-namespace probe (%s host)\n\n", LFS_PROBE_ARCH);

    PIDLIST_ABSOLUTE ours = nullptr;
    HRESULT hr = ::SHParseDisplayName(LFS_SHELL_PATH, nullptr, &ours, 0, nullptr);
    Check(SUCCEEDED(hr) && ours != nullptr, L"shell:::{CLSID} resolves to a PIDL");
    if (!ours) return 1;

    IShellFolder* desktop = nullptr;
    Check(SUCCEEDED(::SHGetDesktopFolder(&desktop)) && desktop, L"desktop folder available");

    bool found = false;
    if (desktop) {
        // SHCONTF_NAVIGATION_ENUM is the flag the navigation pane itself uses.
        IEnumIDList* items = nullptr;
        if (SUCCEEDED(desktop->EnumObjects(
                nullptr, SHCONTF_FOLDERS | SHCONTF_NAVIGATION_ENUM | SHCONTF_INIT_ON_FIRST_NEXT,
                &items)) &&
            items) {
            PITEMID_CHILD child = nullptr;
            ULONG fetched = 0;
            while (items->Next(1, &child, &fetched) == S_OK && fetched == 1) {
                STRRET name{};
                if (SUCCEEDED(desktop->GetDisplayNameOf(child, SHGDN_NORMAL, &name))) {
                    wchar_t buffer[MAX_PATH]{};
                    if (SUCCEEDED(::StrRetToBufW(&name, child, buffer, ARRAYSIZE(buffer))) &&
                        ::StrCmpIW(buffer, LFS_DISPLAY_NAME) == 0) {
                        found = true;
                    }
                }
                ::CoTaskMemFree(child);
                if (found) break;
            }
            items->Release();
        }

        // Same check as above, but through the registry attributes the shell
        // reads before it ever creates our object. These two have to agree, or
        // the node shows up in Explorer and is missing from file dialogs.
        SFGAOF nodeAttrs = SFGAO_FOLDER | SFGAO_FILESYSANCESTOR;
        auto lastId = reinterpret_cast<PCUITEMID_CHILD>(::ILFindLastID(ours));
        const HRESULT attrHr = desktop->GetAttributesOf(1, &lastId, &nodeAttrs);
        Check(SUCCEEDED(attrHr) && (nodeAttrs & SFGAO_FILESYSANCESTOR),
              L"registered attributes include FILESYSANCESTOR");

        desktop->Release();
    }
    Check(found, L"node appears in the desktop's navigation enumeration");

    IShellFolder* node = nullptr;
    hr = ::SHBindToObject(nullptr, ours, nullptr, IID_IShellFolder,
                          reinterpret_cast<void**>(&node));
    Check(SUCCEEDED(hr) && node != nullptr, L"shell binds to our node through COM registration");

    if (node) {
        IEnumIDList* children = nullptr;
        ULONG count = 0;
        if (SUCCEEDED(node->EnumObjects(nullptr, SHCONTF_FOLDERS, &children)) && children) {
            PITEMID_CHILD child = nullptr;
            ULONG fetched = 0;
            while (children->Next(1, &child, &fetched) == S_OK && fetched == 1) {
                STRRET name{};
                if (SUCCEEDED(node->GetDisplayNameOf(child, SHGDN_NORMAL, &name))) {
                    wchar_t buffer[MAX_PATH]{};
                    if (SUCCEEDED(::StrRetToBufW(&name, child, buffer, ARRAYSIZE(buffer)))) {
                        std::wprintf(L"    child: %s\n", buffer);
                    }
                }
                ::CoTaskMemFree(child);
                ++count;
            }
            children->Release();
        }
        Check(count > 0, L"node enumerates its children through the shell");
        node->Release();
    }

    ::CoTaskMemFree(ours);
    return g_failures;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring dllPath = LFS_EXTENSION_DLL_NAME;
    if (argc > 1) dllPath = argv[1];

    if (dllPath == L"--registered") {
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const int failures = ProbeRegistered();
        ::CoUninitialize();
        std::wprintf(L"\n%s (%d failure(s))\n", failures == 0 ? L"PASSED" : L"FAILED", failures);
        return failures == 0 ? 0 : 1;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    std::wprintf(L"Probing %s (%s host)\n\n", dllPath.c_str(), LFS_PROBE_ARCH);

    const HMODULE dll = ::LoadLibraryW(dllPath.c_str());
    if (!dll) {
        std::fwprintf(stderr, L"LoadLibrary failed: %lu\n", ::GetLastError());
        return 2;
    }

    auto getClassObject =
        reinterpret_cast<DllGetClassObjectFn>(::GetProcAddress(dll, "DllGetClassObject"));
    auto canUnload = reinterpret_cast<DllCanUnloadNowFn>(::GetProcAddress(dll, "DllCanUnloadNow"));
    if (!getClassObject || !canUnload) {
        std::fwprintf(stderr, L"Required exports missing\n");
        return 2;
    }

    std::wprintf(L"Class factory\n");
    IClassFactory* factory = nullptr;
    HRESULT hr = getClassObject(CLSID_LastFolderStanding, IID_IClassFactory,
                                reinterpret_cast<void**>(&factory));
    Check(SUCCEEDED(hr) && factory != nullptr, L"DllGetClassObject returns a class factory");
    if (!factory) return 2;

    // A CLSID we do not implement must be refused, not served.
    IClassFactory* other = nullptr;
    hr = getClassObject(CLSID_ShellLink, IID_IClassFactory, reinterpret_cast<void**>(&other));
    Check(hr == CLASS_E_CLASSNOTAVAILABLE && other == nullptr, L"unknown CLSID is refused");

    IShellFolder2* folder = nullptr;
    hr = factory->CreateInstance(nullptr, IID_IShellFolder2, reinterpret_cast<void**>(&folder));
    Check(SUCCEEDED(hr) && folder != nullptr, L"CreateInstance yields IShellFolder2");
    if (!folder) return 2;

    std::wprintf(L"\nIPersistFolder2\n");
    IPersistFolder2* persist = nullptr;
    hr = folder->QueryInterface(IID_IPersistFolder2, reinterpret_cast<void**>(&persist));
    Check(SUCCEEDED(hr) && persist != nullptr, L"IPersistFolder2 available");
    if (persist) {
        PIDLIST_ABSOLUTE desktop = nullptr;
        ::SHGetKnownFolderIDList(FOLDERID_Desktop, 0, nullptr, &desktop);
        Check(SUCCEEDED(persist->Initialize(desktop)), L"Initialize accepts an absolute PIDL");

        PIDLIST_ABSOLUTE current = nullptr;
        Check(SUCCEEDED(persist->GetCurFolder(&current)) && current != nullptr,
              L"GetCurFolder returns the stored PIDL");
        if (current) ::CoTaskMemFree(current);
        if (desktop) ::CoTaskMemFree(desktop);

        CLSID clsid{};
        Check(SUCCEEDED(persist->GetClassID(&clsid)) && clsid == CLSID_LastFolderStanding,
              L"GetClassID reports our CLSID");
        persist->Release();
    }

    std::wprintf(L"\nEnumeration\n");
    IEnumIDList* enumerator = nullptr;
    hr = folder->EnumObjects(nullptr, SHCONTF_FOLDERS, &enumerator);
    Check(SUCCEEDED(hr) && enumerator != nullptr, L"EnumObjects succeeds");

    std::vector<PITEMID_CHILD> children;
    if (enumerator) {
        PITEMID_CHILD child = nullptr;
        ULONG fetched = 0;
        while (enumerator->Next(1, &child, &fetched) == S_OK && fetched == 1) {
            children.push_back(child);
        }
        enumerator->Release();
    }
    std::wprintf(L"  %zu child item(s) from state.json\n", children.size());

    // Files-only enumeration must come back empty, not with our folders.
    IEnumIDList* filesOnly = nullptr;
    if (SUCCEEDED(folder->EnumObjects(nullptr, SHCONTF_NONFOLDERS, &filesOnly)) && filesOnly) {
        PITEMID_CHILD unexpected = nullptr;
        ULONG fetched = 0;
        const HRESULT next = filesOnly->Next(1, &unexpected, &fetched);
        Check(next == S_FALSE && fetched == 0, L"non-folder enumeration is empty");
        if (unexpected) ::CoTaskMemFree(unexpected);
        filesOnly->Release();
    }

    if (!children.empty()) {
        std::wprintf(L"\nChild items\n");
        std::vector<std::wstring> names;
        for (size_t i = 0; i < children.size(); ++i) {
            STRRET display{};
            STRRET parsing{};
            const bool okDisplay = SUCCEEDED(folder->GetDisplayNameOf(children[i], SHGDN_NORMAL,
                                                                      &display));
            const bool okParsing = SUCCEEDED(folder->GetDisplayNameOf(children[i],
                                                                      SHGDN_FORPARSING, &parsing));
            if (okDisplay && okParsing) {
                const std::wstring name = StrRetToString(display, children[i]);
                const std::wstring path = StrRetToString(parsing, children[i]);
                names.push_back(name);
                std::wprintf(L"  %zu. %-28s -> %s\n", i + 1, name.c_str(), path.c_str());
                // The parsing name has to be a real path, otherwise navigation breaks.
                const DWORD attrs = ::GetFileAttributesW(path.c_str());
                if (path.size() > 1 && path[0] != L'\\') {
                    Check(attrs != INVALID_FILE_ATTRIBUTES &&
                              (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0,
                          L"    parsing name is an existing directory");
                }
            } else {
                Check(false, L"GetDisplayNameOf works");
            }
        }

        // Two folders with the same leaf name must not end up with the same
        // label -- that is the whole point of the widened display names.
        bool unique = true;
        for (size_t i = 0; i < names.size() && unique; ++i) {
            for (size_t j = i + 1; j < names.size(); ++j) {
                if (::StrCmpIW(names[i].c_str(), names[j].c_str()) == 0) {
                    std::wprintf(L"    duplicate label: %s\n", names[i].c_str());
                    unique = false;
                    break;
                }
            }
        }
        Check(unique, L"display names are unique within the list");

        PCUITEMID_CHILD first = children[0];
        SFGAOF attrs = SFGAO_FOLDER | SFGAO_FILESYSTEM | SFGAO_HASSUBFOLDER | SFGAO_LINK;
        hr = folder->GetAttributesOf(1, &first, &attrs);
        Check(SUCCEEDED(hr) && (attrs & SFGAO_FOLDER) && (attrs & SFGAO_FILESYSTEM),
              L"child reports FOLDER | FILESYSTEM");
        // Without LINK the shell browses to <our node>\<child>, and going up one
        // level lands on our node instead of the target's real parent.
        Check(SUCCEEDED(hr) && (attrs & SFGAO_LINK), L"child reports LINK");

        // A file dialog asking for real paths (FOS_FORCEFILESYSTEM) drops any
        // node that is neither a filesystem object nor able to contain one.
        SFGAOF rootAttrs = SFGAO_FOLDER | SFGAO_FILESYSANCESTOR;
        hr = folder->GetAttributesOf(0, nullptr, &rootAttrs);
        Check(SUCCEEDED(hr) && (rootAttrs & SFGAO_FILESYSANCESTOR),
              L"folder itself reports FILESYSANCESTOR");

        IExtractIconW* icon = nullptr;
        hr = folder->GetUIObjectOf(nullptr, 1, &first, IID_IExtractIconW, nullptr,
                                   reinterpret_cast<void**>(&icon));
        Check(SUCCEEDED(hr) && icon != nullptr, L"GetUIObjectOf provides IExtractIconW");
        if (icon) {
            wchar_t iconFile[MAX_PATH]{};
            int index = 0;
            UINT flags = 0;
            Check(SUCCEEDED(icon->GetIconLocation(0, iconFile, ARRAYSIZE(iconFile), &index,
                                                  &flags)),
                  L"GetIconLocation succeeds");
            std::wprintf(L"    icon: %s,%d\n", iconFile, index);
            icon->Release();
        }

        // What makes "up one level" leave our node: the shell follows the link
        // and browses to the target's own place in the namespace. If GetIDList
        // comes back empty here, it has nothing to navigate to and falls back to
        // the old behaviour.
        for (int viaBind = 0; viaBind < 2; ++viaBind) {
            IShellLinkW* link = nullptr;
            const HRESULT linkHr =
                viaBind ? folder->BindToObject(children[0], nullptr, IID_IShellLinkW,
                                               reinterpret_cast<void**>(&link))
                        : folder->GetUIObjectOf(nullptr, 1, &first, IID_IShellLinkW, nullptr,
                                                reinterpret_cast<void**>(&link));
            Check(SUCCEEDED(linkHr) && link != nullptr,
                  viaBind ? L"BindToObject provides IShellLink" : L"GetUIObjectOf provides IShellLink");
            if (!link) continue;

            PIDLIST_ABSOLUTE target = nullptr;
            wchar_t targetPath[MAX_PATH]{};
            const bool resolved = SUCCEEDED(link->GetIDList(&target)) && target != nullptr &&
                                  ::SHGetPathFromIDListW(target, targetPath);
            Check(resolved, L"    link resolves to the target folder");
            if (resolved) std::wprintf(L"    link target: %s\n", targetPath);
            if (target) ::CoTaskMemFree(target);
            link->Release();
        }

        IShellFolder* real = nullptr;
        hr = folder->BindToObject(children[0], nullptr, IID_IShellFolder,
                                  reinterpret_cast<void**>(&real));
        Check(SUCCEEDED(hr) && real != nullptr, L"BindToObject binds to the real folder");
        if (real) {
            IEnumIDList* realItems = nullptr;
            if (SUCCEEDED(real->EnumObjects(nullptr, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS,
                                            &realItems)) &&
                realItems) {
                ULONG count = 0;
                PITEMID_CHILD item = nullptr;
                ULONG fetched = 0;
                while (realItems->Next(1, &item, &fetched) == S_OK && fetched == 1) {
                    ::CoTaskMemFree(item);
                    if (++count >= 5) break;
                }
                std::wprintf(L"    target folder enumerates (%lu+ items)\n", count);
                realItems->Release();
            }
            real->Release();
        }

        if (children.size() >= 2) {
            const HRESULT cmp = folder->CompareIDs(0, children[0], children[1]);
            Check(SUCCEEDED(cmp) && static_cast<short>(HRESULT_CODE(cmp)) < 0,
                  L"CompareIDs keeps state.json order");
        }
    }

    std::wprintf(L"\nMalformed input\n");
    struct BogusCase {
        const wchar_t* name;
        USHORT cb;
        USHORT signature;
        USHORT version;
        bool terminate;
    };
    const BogusCase cases[] = {
        {L"foreign signature rejected", 32, 0x1234, lfs::kChildVersion, true},
        {L"undersized item rejected", 4, lfs::kChildSignature, lfs::kChildVersion, false},
        {L"unterminated path rejected", 40, lfs::kChildSignature, lfs::kChildVersion, false},
        // An item from an older build has the path at a different offset.
        {L"outdated item version rejected", 40, lfs::kChildSignature, 1, true},
    };
    for (const auto& c : cases) {
        PITEMID_CHILD bogus = MakeBogusPidl(c.cb, c.signature, c.version, c.terminate);
        if (!bogus) continue;
        STRRET name{};
        const HRESULT nameHr = folder->GetDisplayNameOf(bogus, SHGDN_NORMAL, &name);
        PCUITEMID_CHILD bogusConst = bogus;
        SFGAOF flags = SFGAO_FOLDER;
        const HRESULT attrHr = folder->GetAttributesOf(1, &bogusConst, &flags);
        void* unused = nullptr;
        const HRESULT bindHr = folder->BindToObject(bogus, nullptr, IID_IShellFolder, &unused);
        Check(FAILED(nameHr) && FAILED(attrHr) && FAILED(bindHr), c.name);
        ::CoTaskMemFree(bogus);
    }

    STRRET nullName{};
    Check(FAILED(folder->GetDisplayNameOf(nullptr, SHGDN_NORMAL, &nullName)),
          L"null PIDL rejected");
    Check(folder->GetDisplayNameOf(children.empty() ? nullptr : children[0], SHGDN_NORMAL,
                                   nullptr) == E_POINTER,
          L"null out-pointer rejected");

    std::wprintf(L"\nLifetime\n");
    for (auto* child : children) ::CoTaskMemFree(child);
    folder->Release();
    factory->Release();
    Check(canUnload() == S_OK, L"DllCanUnloadNow is S_OK after releasing everything");

    ::CoUninitialize();
    ::FreeLibrary(dll);

    std::wprintf(L"\n%s (%d failure(s))\n", g_failures == 0 ? L"PASSED" : L"FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
