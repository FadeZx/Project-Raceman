#pragma once
#include <functional>
#include <string>
#include <vector>
#include <mutex>

namespace raceman {

enum class MessageType : int { Log = 0, Warning = 1, Error = 2 };

struct ConsoleEntry {
    MessageType type;
    std::string text;
};

class Console {
public:
    void AddLog(const std::string& msg)    { Add(MessageType::Log, msg); }
    void AddWarning(const std::string& msg){ Add(MessageType::Warning, msg); }
    void AddError(const std::string& msg)  { Add(MessageType::Error, msg); }

    // Draws the console panel. Returns true if any UI state changed.
    bool RenderPanel(const char* title = "Console");
    bool RenderContents();
    // Flat log view: all entries in one scrolling list (no tabs). Used by the player debug overlay.
    void RenderFlat();

    // Optional: external toggle for auto-scroll
    void SetAutoScroll(bool enabled) { autoScroll_ = enabled; }
    void SetCommandHandler(std::function<bool(const std::string&)> handler) { commandHandler_ = std::move(handler); }

private:
    void Add(MessageType type, const std::string& msg);

    enum class Tab { Log = 0, Warning = 1, Error = 2 };

    std::vector<ConsoleEntry> entries_;
    // legacy toggles kept for compatibility, but UI now uses tabs
    bool showLog_{true};
    bool showWarning_{true};
    bool showError_{true};
    bool autoScroll_{true};
    std::string input_;
    std::function<bool(const std::string&)> commandHandler_;
    std::mutex mtx_;
    Tab currentTab_{Tab::Log};
};

} // namespace raceman
