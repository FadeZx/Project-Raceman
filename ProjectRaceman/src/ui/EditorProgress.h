#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace raceman {

class EditorProgressService;

class EditorProgressTask {
public:
    EditorProgressTask() = default;

    bool IsValid() const { return static_cast<bool>(state_); }
    bool IsCancellationRequested() const;
    bool HasBeenRendered() const;
    void SetMessage(const std::string& message) const;
    void SetDetail(const std::string& detail) const;
    void SetProgress(int completed, int total) const;
    void SetProgress(float fraction) const;
    void SetIndeterminate(bool indeterminate = true) const;
    void Complete(const std::string& detail = {}) const;
    void Fail(const std::string& detail) const;
    void Cancelled(const std::string& detail = "Cancelled.") const;

private:
    struct State {
        enum class Result { Running, Succeeded, Failed, Cancelled };

        std::uint64_t id{0};
        std::string title;
        std::string message;
        std::string detail;
        mutable std::mutex textMutex;
        std::atomic<int> completed{0};
        std::atomic<int> total{0};
        std::atomic<bool> indeterminate{true};
        std::atomic<bool> cancellable{false};
        std::atomic<bool> cancelRequested{false};
        std::atomic<bool> rendered{false};
        std::atomic<bool> dismissed{false};
        std::atomic<Result> result{Result::Running};
        std::chrono::steady_clock::time_point startedAt{std::chrono::steady_clock::now()};
        std::chrono::steady_clock::time_point finishedAt{};
        std::chrono::steady_clock::time_point completionFirstRenderedAt{};
        mutable std::mutex finishMutex;
    };

    explicit EditorProgressTask(std::shared_ptr<State> state) : state_(std::move(state)) {}
    void Finish(State::Result result, const std::string& detail) const;

    std::shared_ptr<State> state_;
    friend class EditorProgressService;
};

class EditorProgressService {
public:
    static EditorProgressService& Get();

    EditorProgressTask Begin(const std::string& title,
                             const std::string& message,
                             int totalSteps = 0,
                             bool cancellable = false);
    void Render();
    void CancelAll();

private:
    EditorProgressService() = default;
    std::shared_ptr<EditorProgressTask::State> CurrentTask();
    void RemoveFinishedTasks();

    std::mutex mutex_;
    std::vector<std::shared_ptr<EditorProgressTask::State>> tasks_;
    std::atomic<std::uint64_t> nextId_{1};
};

// RAII helper for short synchronous operations. Use Begin() directly when the
// operation continues on another frame or worker thread.
class ScopedEditorProgress {
public:
    ScopedEditorProgress(const std::string& title, const std::string& message)
        : task_(EditorProgressService::Get().Begin(title, message)) {}
    ~ScopedEditorProgress() { task_.Complete(); }
    EditorProgressTask& Task() { return task_; }

private:
    EditorProgressTask task_;
};

} // namespace raceman
