#pragma once

#include <atomic>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace raceman {

struct RecentProject {
    std::string name;
    std::string path;
    time_t lastOpened{0};
};

class ProjectLauncher {
public:
    using OnProjectOpened = std::function<void(const std::string& path)>;

    ProjectLauncher();
    ~ProjectLauncher();

    // Call each frame. Invokes onProjectOpened(path) when user selects a project.
    void Render(const OnProjectOpened& onProjectOpened);

    // Registry helpers — callable from Application too.
    static void AddToRegistry(const std::string& name, const std::string& path);
    static std::vector<RecentProject> LoadRegistry();

private:
    static std::string RegistryPath();
    static void SaveRegistry(const std::vector<RecentProject>& projects);

    void StartPicker(bool forNew);
    void TickPicker(const OnProjectOpened& cb);
    void TryOpenExisting(const std::string& folder, const OnProjectOpened& cb);
    void TryCreateNew(const std::string& parentFolder, const std::string& name,
                      const OnProjectOpened& cb);

    std::vector<RecentProject> projects_;
    bool loaded_{false};

    struct PickerState {
        std::atomic<bool> isDone{false};
        std::mutex mu;
        std::string result;
    };
    std::unique_ptr<std::thread> pickerThread_;
    std::shared_ptr<PickerState> pickerState_;
    bool pickerForNew_{false};

    char newNameBuf_[128]{};
    std::string newParentPath_;
    bool showNewDialog_{false};
    std::string errorMsg_;
};

} // namespace raceman
