#include "ProjectLauncher.h"

#include <imgui/imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace fs = std::filesystem;

namespace raceman {

namespace {

std::string PickFolder(const wchar_t* title) {
#if defined(_WIN32)
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool doUninit = SUCCEEDED(coInit);
    IFileDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg))) || !dlg) {
        if (doUninit) CoUninitialize();
        return {};
    }
    DWORD opts = 0;
    if (SUCCEEDED(dlg->GetOptions(&opts)))
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(title);
    std::string result;
    if (SUCCEEDED(dlg->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR wide = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    result.resize(static_cast<size_t>(size - 1));
                    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), size, nullptr, nullptr);
                }
                CoTaskMemFree(wide);
            }
            item->Release();
        }
    }
    dlg->Release();
    if (doUninit) CoUninitialize();
    return result;
#else
    (void)title;
    return {};
#endif
}

std::string FormatRelativeTime(time_t then) {
    if (then == 0) return "";
    const time_t now = std::time(nullptr);
    const double diff = std::difftime(now, then);
    if (diff < 120)            return "just now";
    if (diff < 3600)           return std::to_string(int(diff / 60))   + " min ago";
    if (diff < 7200)           return "1 hour ago";
    if (diff < 86400)          return std::to_string(int(diff / 3600)) + " hours ago";
    if (diff < 172800)         return "yesterday";
    if (diff < 86400.0 * 30)   return std::to_string(int(diff / 86400)) + " days ago";
    char buf[32]{};
#if defined(_WIN32)
    tm t{};
    if (localtime_s(&t, &then) == 0) std::strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
#else
    tm t{};
    if (localtime_r(&then, &t))      std::strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
#endif
    return buf;
}

// ---- Minimal JSON helpers for the registry ----

std::string EscapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        if      (c == '\\') out += "\\\\";
        else if (c == '"')  out += "\\\"";
        else                out += c;
    }
    return out;
}

std::string UnescapeJson(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool esc = false;
    for (char c : s) {
        if (esc) {
            if      (c == '\\') out += '\\';
            else if (c == '"')  out += '"';
            else if (c == 'n')  out += '\n';
            else { out += '\\'; out += c; }
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else {
            out += c;
        }
    }
    return out;
}

std::string ExtractJsonStr(const std::string& src, const std::string& key) {
    const std::string needle = "\"" + key + "\":\"";
    auto pos = src.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    std::string val;
    bool esc = false;
    for (; pos < src.size(); ++pos) {
        const char c = src[pos];
        if (esc) { val += c; esc = false; }
        else if (c == '\\') esc = true;
        else if (c == '"')  break;
        else val += c;
    }
    return UnescapeJson(val);
}

time_t ExtractJsonInt(const std::string& src, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = src.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t')) ++pos;
    std::string digits;
    while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos])))
        digits += src[pos++];
    if (digits.empty()) return 0;
    return static_cast<time_t>(std::stoll(digits));
}

} // namespace

// ---- Registry ----

std::string ProjectLauncher::RegistryPath() {
    return "config/recent_projects.json";
}

std::vector<RecentProject> ProjectLauncher::LoadRegistry() {
    std::vector<RecentProject> result;
    std::ifstream f(RegistryPath());
    if (!f.good()) return result;
    const std::string content((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
    const auto arrStart = content.find('[');
    const auto arrEnd   = content.rfind(']');
    if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart)
        return result;
    const std::string arr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
    size_t cursor = 0;
    while (cursor < arr.size()) {
        const auto objStart = arr.find('{', cursor);
        if (objStart == std::string::npos) break;
        const auto objEnd = arr.find('}', objStart);
        if (objEnd == std::string::npos) break;
        const std::string obj = arr.substr(objStart, objEnd - objStart + 1);
        RecentProject p;
        p.name       = ExtractJsonStr(obj, "name");
        p.path       = ExtractJsonStr(obj, "path");
        p.lastOpened = ExtractJsonInt(obj, "ts");
        if (!p.path.empty()) result.push_back(std::move(p));
        cursor = objEnd + 1;
    }
    return result;
}

void ProjectLauncher::SaveRegistry(const std::vector<RecentProject>& projects) {
    try { fs::create_directories(fs::path(RegistryPath()).parent_path()); } catch (...) {}
    std::ofstream f(RegistryPath(), std::ios::trunc);
    if (!f.good()) return;
    f << "{\"projects\":[";
    for (size_t i = 0; i < projects.size(); ++i) {
        const auto& p = projects[i];
        if (i > 0) f << ',';
        f << "{\"name\":\"" << EscapeJson(p.name)
          << "\",\"path\":\"" << EscapeJson(p.path)
          << "\",\"ts\":"    << static_cast<long long>(p.lastOpened) << "}";
    }
    f << "]}";
}

void ProjectLauncher::AddToRegistry(const std::string& name, const std::string& path) {
    if (path.empty()) return;
    auto projects = LoadRegistry();
    projects.erase(std::remove_if(projects.begin(), projects.end(),
        [&](const RecentProject& p) { return p.path == path; }), projects.end());
    RecentProject entry;
    entry.name       = name.empty() ? fs::path(path).filename().string() : name;
    entry.path       = path;
    entry.lastOpened = std::time(nullptr);
    projects.insert(projects.begin(), std::move(entry));
    if (projects.size() > 20) projects.resize(20);
    SaveRegistry(projects);
}

// ---- Constructor / Destructor ----

ProjectLauncher::ProjectLauncher() {
    std::snprintf(newNameBuf_, sizeof(newNameBuf_), "NewProject");
}

ProjectLauncher::~ProjectLauncher() {
    if (pickerThread_ && pickerThread_->joinable())
        pickerThread_->join();
}

// ---- Async folder picker ----

void ProjectLauncher::StartPicker(bool forNew) {
    if (pickerState_ && !pickerState_->isDone.load()) return;
    pickerForNew_ = forNew;
    auto state = std::make_shared<PickerState>();
    pickerState_ = state;
    const wchar_t* title = forNew
        ? L"Choose parent folder for new project"
        : L"Choose existing project folder";
    pickerThread_ = std::make_unique<std::thread>([state, title]() {
        const std::string folder = PickFolder(title);
        std::lock_guard<std::mutex> lock(state->mu);
        state->result = folder;
        state->isDone.store(true);
    });
}

void ProjectLauncher::TickPicker(const OnProjectOpened& cb) {
    if (!pickerState_ || !pickerState_->isDone.load()) return;
    if (pickerThread_ && pickerThread_->joinable()) pickerThread_->join();
    std::string folder;
    { std::lock_guard<std::mutex> lock(pickerState_->mu); folder = pickerState_->result; }
    pickerThread_.reset();
    pickerState_.reset();
    if (folder.empty()) return;
    if (pickerForNew_) {
        newParentPath_ = folder;
        showNewDialog_ = true;
        errorMsg_.clear();
    } else {
        TryOpenExisting(folder, cb);
    }
}

void ProjectLauncher::TryOpenExisting(const std::string& folder, const OnProjectOpened& cb) {
    errorMsg_.clear();
    if (!fs::exists(folder)) { errorMsg_ = "Folder does not exist."; return; }
    try { fs::create_directories(fs::path(folder) / "assets" / "scenes"); } catch (...) {}
    if (cb) cb(folder);
}

void ProjectLauncher::TryCreateNew(const std::string& parentFolder,
                                   const std::string& name,
                                   const OnProjectOpened& cb) {
    errorMsg_.clear();
    if (name.empty())             { errorMsg_ = "Project name cannot be empty."; return; }
    if (!fs::exists(parentFolder)) { errorMsg_ = "Parent folder does not exist."; return; }
    const fs::path projectPath = fs::path(parentFolder) / name;
    try {
        fs::create_directories(projectPath / "assets" / "scenes");
        fs::create_directories(projectPath / "assets" / "scripts");
    } catch (const std::exception& ex) {
        errorMsg_ = std::string("Failed to create folders: ") + ex.what();
        return;
    }
    const fs::path projFile = projectPath / "project.raceman.json";
    if (!fs::exists(projFile)) {
        std::ofstream pf(projFile);
        if (pf) {
            pf << "{\n"
               << "  \"version\": 1,\n"
               << "  \"projectName\": \"" << name << "\",\n"
               << "  \"assetsRoot\": \"assets\",\n"
               << "  \"defaultScene\": \"assets/scenes/Main.scene.json\",\n"
               << "  \"lastScene\": \"assets/scenes/Main.scene.json\"\n"
               << "}\n";
        }
    }
    if (cb) cb(projectPath.string());
}

// ---- Render ----

void ProjectLauncher::Render(const OnProjectOpened& onProjectOpened) {
    if (!loaded_) {
        projects_ = LoadRegistry();
        loaded_ = true;
    }
    TickPicker(onProjectOpened);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##Launcher", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(3);

    const float W        = vp->Size.x;
    const float H        = vp->Size.y;
    const float sidebarW = 220.0f;

    // ── Left sidebar ──────────────────────────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    ImGui::SetCursorPos(ImVec2(0, 0));
    ImGui::BeginChild("##Sidebar", ImVec2(sidebarW, H), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::Dummy(ImVec2(1, 28));
    ImGui::SetCursorPosX(20.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
    ImGui::TextUnformatted("Project Raceman");
    ImGui::PopStyleColor();
    ImGui::SetCursorPosX(20.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.50f, 0.55f, 1.0f));
    ImGui::TextUnformatted("Engine");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(1, 16));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.22f, 0.22f, 0.26f, 1.0f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(1, 14));

    const bool pickerBusy = pickerState_ && !pickerState_->isDone.load();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 9));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.20f, 0.45f, 0.80f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.16f, 0.38f, 0.68f, 1.0f));
    ImGui::SetCursorPosX(14.0f);
    ImGui::BeginDisabled(pickerBusy || showNewDialog_);
    if (ImGui::Button("New Project", ImVec2(sidebarW - 28.0f, 0)))
        StartPicker(true);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(1, 6));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 9));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.18f, 0.20f, 0.24f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.28f, 0.34f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.16f, 0.20f, 1.0f));
    ImGui::SetCursorPosX(14.0f);
    ImGui::BeginDisabled(pickerBusy || showNewDialog_);
    if (ImGui::Button("Open Project", ImVec2(sidebarW - 28.0f, 0)))
        StartPicker(false);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();

    if (pickerBusy) {
        ImGui::Dummy(ImVec2(1, 8));
        ImGui::SetCursorPosX(20.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
        ImGui::TextUnformatted("Picking folder...");
        ImGui::PopStyleColor();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg

    // ── Right panel — recent projects ─────────────────────────────────────────
    ImGui::SetCursorPos(ImVec2(sidebarW, 0));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.13f, 0.13f, 0.15f, 1.0f));
    ImGui::BeginChild("##Content", ImVec2(W - sidebarW, H), false);

    ImGui::Dummy(ImVec2(1, 14));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 14.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.88f, 0.88f, 1.0f));
    ImGui::TextUnformatted("Recent Projects");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.25f, 0.30f, 1.0f));
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(1, 4));

    if (projects_.empty()) {
        ImGui::Dummy(ImVec2(1, 24));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.48f, 0.52f, 1.0f));
        ImGui::TextUnformatted("No recent projects.");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16.0f);
        ImGui::TextUnformatted("Use 'New Project' or 'Open Project' to get started.");
        ImGui::PopStyleColor();
    }

    int toRemove = -1;
    const float contentW = W - sidebarW;
    const float removeW  = 28.0f;
    const float timeW    = 100.0f;
    const float rowH     = 62.0f;

    for (int i = 0; i < static_cast<int>(projects_.size()); ++i) {
        const RecentProject& p = projects_[i];
        const bool exists = fs::exists(p.path);
        ImGui::PushID(i);

        // Row background
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const ImVec2 rowMax = ImVec2(rowMin.x + contentW - 4.0f, rowMin.y + rowH);
        const bool hovered  = ImGui::IsMouseHoveringRect(rowMin, rowMax);
        if (hovered) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                rowMin, rowMax, IM_COL32(40, 42, 52, 255));
        }

        // Name + path click target
        const float nameW = contentW - removeW - timeW - 16.0f;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0, 0, 0, 0));
        if (!exists) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
        if (ImGui::Selectable("##sel", false, ImGuiSelectableFlags_None, ImVec2(nameW, rowH))
                && exists && onProjectOpened) {
            projects_[i].lastOpened = std::time(nullptr);
            SaveRegistry(projects_);
            onProjectOpened(p.path);
        }
        if (!exists) ImGui::PopStyleColor();
        ImGui::PopStyleColor(3);

        // Overlay text
        const float baseX = ImGui::GetItemRectMin().x + 14.0f;
        const float baseY = ImGui::GetItemRectMin().y;
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(baseX, baseY + 12.0f),
            exists ? IM_COL32(230, 230, 230, 255) : IM_COL32(120, 120, 120, 255),
            p.name.c_str());
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(baseX, baseY + 34.0f),
            exists ? IM_COL32(130, 130, 145, 255) : IM_COL32(160, 80, 80, 255),
            exists ? p.path.c_str() : (p.path + "  (missing)").c_str());

        // Time label
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetTextLineHeight()) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.48f, 0.55f, 1.0f));
        ImGui::TextUnformatted(FormatRelativeTime(p.lastOpened).c_str());
        ImGui::PopStyleColor();

        // Remove (x) button
        ImGui::SameLine();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.55f, 0.15f, 0.15f, 0.7f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.60f, 0.38f, 0.38f, 1.0f));
        if (ImGui::SmallButton("x")) toRemove = i;
        ImGui::PopStyleColor(4);

        // Divider
        ImGui::SetCursorPosY(rowMin.y + rowH);
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.20f, 0.20f, 0.24f, 1.0f));
        ImGui::Separator();
        ImGui::PopStyleColor();

        ImGui::PopID();
    }

    if (toRemove >= 0) {
        projects_.erase(projects_.begin() + toRemove);
        SaveRegistry(projects_);
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(); // ChildBg

    ImGui::End();

    // ── New Project dialog ────────────────────────────────────────────────────
    if (showNewDialog_) {
        const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Always);
        ImGui::OpenPopup("New Project##newdlg");
    }

    if (ImGui::BeginPopupModal("New Project##newdlg", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextUnformatted("Parent folder:");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.60f, 1.0f));
        ImGui::TextUnformatted(newParentPath_.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextUnformatted("Project name:");
        ImGui::SetNextItemWidth(-1.0f);
        const bool enter = ImGui::InputText("##newname", newNameBuf_, sizeof(newNameBuf_),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (!errorMsg_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.40f, 0.40f, 1.0f));
            ImGui::TextUnformatted(errorMsg_.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        const std::string preview = (fs::path(newParentPath_) / newNameBuf_).string();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.48f, 0.55f, 1.0f));
        ImGui::TextUnformatted(("Path: " + preview).c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Create", ImVec2(110, 0)) || enter) {
            TryCreateNew(newParentPath_, newNameBuf_, onProjectOpened);
            if (errorMsg_.empty()) {
                showNewDialog_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)) ||
                (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape))) {
            showNewDialog_ = false;
            errorMsg_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace raceman
