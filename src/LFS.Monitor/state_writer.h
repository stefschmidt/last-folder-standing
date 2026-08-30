// Publishes state.json -- the only contract between monitor and shell extension
// (docs/DEVELOPMENT.md rule 4). Written atomically and only when the content really changed,
// because every write pokes the shell into re-reading it.
#pragma once

#include <string>
#include <vector>

#include "folder_entry.h"

namespace lfs {

inline constexpr int kStateVersion = 1;

class StateWriter {
public:
    // Reads the existing state.json so a restart with an unchanged list does not
    // produce a pointless write + shell notification.
    void LoadExisting();

    // Returns true if the file was rewritten.
    bool Write(const std::vector<FolderEntry>& folders);

    static std::wstring Serialize(const std::vector<FolderEntry>& folders);

private:
    std::vector<FolderEntry> last_;
    bool haveLast_ = false;
};

}  // namespace lfs
