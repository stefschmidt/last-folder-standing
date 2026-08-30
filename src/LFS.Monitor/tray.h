// Tray icon and its menu. Owns a hidden window and must be created and
// destroyed on the thread that runs the message loop.
#pragma once

#include <windows.h>

#include <functional>
#include <mutex>
#include <string>

namespace lfs {

enum class TrayCommand { OpenSettings, TogglePause, Quit };

class TrayIcon {
public:
    ~TrayIcon();

    bool Create(const std::function<void(TrayCommand)>& onCommand);
    void Destroy();

    // Safe to call from any thread.
    void SetStatus(const std::wstring& topFolder, bool paused);

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void ShowMenu();
    void ApplyStatus();

    HWND hwnd_ = nullptr;
    HICON icon_ = nullptr;
    bool iconAdded_ = false;
    bool paused_ = false;
    std::function<void(TrayCommand)> onCommand_;

    std::mutex statusMutex_;
    std::wstring topFolder_;
};

}  // namespace lfs
