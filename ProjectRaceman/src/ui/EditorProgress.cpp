#include "EditorProgress.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace raceman {
namespace {

// Terminal trace for the modal progress popups. The popup gates blocking work
// (see HasBeenRendered), so knowing when it actually reached the screen is the
// only way to tell a slow save from a popup that never became visible.
int ElapsedMs(std::chrono::steady_clock::time_point from) {
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - from).count());
}

void ProgressLog(std::uint64_t id, const char* event, int elapsedMs,
                 const std::string& title, const std::string& extra) {
    std::printf("[progress] #%llu %-10s +%5dms %s%s%s\n",
                static_cast<unsigned long long>(id), event, elapsedMs,
                title.c_str(), extra.empty() ? "" : " | ", extra.c_str());
    std::fflush(stdout);
}

} // namespace

bool EditorProgressTask::IsCancellationRequested() const {
    return state_ && state_->cancelRequested.load();
}

bool EditorProgressTask::HasBeenRendered() const {
    // ImGui hides an auto-resizing window on its appearing frame while it measures
    // the layout, so "the popup was submitted" is not the same as "the popup was
    // drawn". Gate on a frame that actually produced pixels and was presented,
    // otherwise the blocking caller freezes the app before anything is on screen.
    return state_ && state_->visibleFrames.load() >= 1;
}

void EditorProgressTask::SetTitle(const std::string& title) const {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->textMutex);
    state_->title = title.empty() ? "Processing" : title;
}

void EditorProgressTask::SetMessage(const std::string& message) const {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->textMutex);
    state_->message = message;
}

void EditorProgressTask::SetDetail(const std::string& detail) const {
    if (!state_) return;
    std::lock_guard<std::mutex> lock(state_->textMutex);
    state_->detail = detail;
}

void EditorProgressTask::SetProgress(int completed, int total) const {
    if (!state_) return;
    const int safeTotal = (std::max)(0, total);
    state_->total.store(safeTotal);
    state_->completed.store(safeTotal > 0 ? (std::clamp)(completed, 0, safeTotal) : (std::max)(0, completed));
    state_->indeterminate.store(total <= 0);
}

void EditorProgressTask::SetProgress(float fraction) const {
    if (!state_) return;
    state_->completed.store(static_cast<int>((std::clamp)(fraction, 0.0f, 1.0f) * 1000.0f));
    state_->total.store(1000);
    state_->indeterminate.store(false);
}

void EditorProgressTask::SetIndeterminate(bool indeterminate) const {
    if (state_) state_->indeterminate.store(indeterminate);
}

void EditorProgressTask::SetCancellable(bool cancellable) const {
    if (state_) state_->cancellable.store(cancellable);
}

void EditorProgressTask::Finish(State::Result result, const std::string& detail) const {
    if (!state_) return;
    if (!detail.empty()) SetDetail(detail);
    State::Result expected = State::Result::Running;
    if (state_->result.compare_exchange_strong(expected, result)) {
        {
            std::lock_guard<std::mutex> lock(state_->finishMutex);
            state_->finishedAt = std::chrono::steady_clock::now();
        }
        const char* label = result == State::Result::Succeeded ? "succeeded" :
            (result == State::Result::Cancelled ? "cancelled" : "failed");
        std::lock_guard<std::mutex> textLock(state_->textMutex);
        ProgressLog(state_->id, label, ElapsedMs(state_->startedAt), state_->title, state_->detail);
    }
}

void EditorProgressTask::Complete(const std::string& detail) const { Finish(State::Result::Succeeded, detail); }
void EditorProgressTask::Fail(const std::string& detail) const { Finish(State::Result::Failed, detail); }
void EditorProgressTask::Cancelled(const std::string& detail) const { Finish(State::Result::Cancelled, detail); }

EditorProgressService& EditorProgressService::Get() {
    static EditorProgressService service;
    return service;
}

EditorProgressTask EditorProgressService::Begin(const std::string& title,
                                                const std::string& message,
                                                int totalSteps,
                                                bool cancellable) {
    auto state = std::make_shared<EditorProgressTask::State>();
    state->id = nextId_.fetch_add(1);
    state->title = title.empty() ? "Processing" : title;
    state->message = message;
    state->total.store((std::max)(0, totalSteps));
    state->indeterminate.store(totalSteps <= 0);
    state->cancellable.store(cancellable);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(state);
    }
    ProgressLog(state->id, "begin", 0, state->title, message);
    return EditorProgressTask(std::move(state));
}

std::shared_ptr<EditorProgressTask::State> EditorProgressService::CurrentTask() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty()) return {};
    auto running = std::find_if(tasks_.rbegin(), tasks_.rend(), [](const auto& task) {
        return task->result.load() == EditorProgressTask::State::Result::Running;
    });
    return running != tasks_.rend() ? *running : tasks_.back();
}

void EditorProgressService::RemoveFinishedTasks() {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const auto& task) {
        return task->dismissed.load();
    }), tasks_.end());
}

void EditorProgressService::Render() {
    RemoveFinishedTasks();
    auto task = CurrentTask();
    if (!task) return;

    task->rendered.store(true);
    std::string title;
    std::string message;
    std::string detail;
    {
        std::lock_guard<std::mutex> lock(task->textMutex);
        title = task->title;
        message = task->message;
        detail = task->detail;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Always);
    ImGui::OpenPopup("###EditorProgressWindow");
    const std::string popupTitle = title + "###EditorProgressWindow";
    if (!ImGui::BeginPopupModal(popupTitle.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar)) return;

    // The appearing frame is laid out but not rasterized; only later frames count
    // as "the user saw the popup".
    if (!ImGui::IsWindowAppearing() && task->visibleFrames.fetch_add(1) == 0) {
        std::lock_guard<std::mutex> lock(task->finishMutex);
        task->firstVisibleAt = std::chrono::steady_clock::now();
        ProgressLog(task->id, "visible", ElapsedMs(task->startedAt), title, detail);
    }

    if (!message.empty()) ImGui::TextWrapped("%s", message.c_str());
    ImGui::Spacing();
    const auto result = task->result.load();
    if (result != EditorProgressTask::State::Result::Running) {
        const char* resultLabel = result == EditorProgressTask::State::Result::Succeeded ? "Complete" :
            (result == EditorProgressTask::State::Result::Cancelled ? "Cancelled" : "Failed");
        ImGui::ProgressBar(result == EditorProgressTask::State::Result::Succeeded ? 1.0f : 0.0f,
                           ImVec2(-1.0f, 0.0f), resultLabel);
    } else if (task->indeterminate.load()) {
        const float pulse = static_cast<float>(std::fmod(ImGui::GetTime() * 0.45, 1.0));
        ImGui::ProgressBar(pulse, ImVec2(-1.0f, 0.0f), "Please wait");
    } else {
        const int completed = task->completed.load();
        const int total = (std::max)(1, task->total.load());
        const float fraction = (std::clamp)(static_cast<float>(completed) / static_cast<float>(total), 0.0f, 1.0f);
        char label[64];
        std::snprintf(label, sizeof(label), "%d / %d", completed, total);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label);
    }
    if (!detail.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", detail.c_str());
    }

    if (result == EditorProgressTask::State::Result::Running && task->cancellable.load()) {
        ImGui::Spacing();
        if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) task->cancelRequested.store(true);
    } else if (result == EditorProgressTask::State::Result::Failed) {
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100.0f, 0.0f))) {
            task->result.store(EditorProgressTask::State::Result::Cancelled);
            {
                std::lock_guard<std::mutex> lock(task->finishMutex);
                task->finishedAt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            }
            task->dismissed.store(true);
            ProgressLog(task->id, "closed", ElapsedMs(task->startedAt), title, detail);
            ImGui::CloseCurrentPopup();
        }
    } else {
        std::lock_guard<std::mutex> lock(task->finishMutex);
        const auto now = std::chrono::steady_clock::now();
        if (task->completionFirstRenderedAt.time_since_epoch().count() == 0) {
            task->completionFirstRenderedAt = now;
        }
        if (now - task->completionFirstRenderedAt > std::chrono::milliseconds(450)) {
            task->dismissed.store(true);
            const auto visibleFrom = task->firstVisibleAt.time_since_epoch().count() == 0
                ? task->startedAt : task->firstVisibleAt;
            ProgressLog(task->id, "dismissed", ElapsedMs(task->startedAt), title,
                        "visible for " + std::to_string(ElapsedMs(visibleFrom)) + "ms over " +
                        std::to_string(task->visibleFrames.load()) + " frames");
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void EditorProgressService::CancelAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& task : tasks_) task->cancelRequested.store(true);
}

} // namespace raceman
