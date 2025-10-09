#pragma once
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

    // Optional: external toggle for auto-scroll
    void SetAutoScroll(bool enabled) { autoScroll_ = enabled; }

private:
    void Add(MessageType type, const std::string& msg);

    std::vector<ConsoleEntry> entries_;
    bool showLog_{true};
    bool showWarning_{true};
    bool showError_{true};
    bool autoScroll_{true};
    std::string input_;
    std::mutex mtx_;
};

} // namespace raceman