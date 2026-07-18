#include "NativeDialogs.h"

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

namespace raceman {

namespace {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
std::string ShellItemPath(IShellItem* item) {
    if (item == nullptr) return {};

    PWSTR widePath = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &widePath)) || widePath == nullptr) return {};

    std::string result;
    const int size = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
        result.resize(static_cast<size_t>(size));
        WideCharToMultiByte(CP_UTF8, 0, widePath, -1, result.data(), size, nullptr, nullptr);
        result.resize(static_cast<size_t>(size - 1));
    }
    CoTaskMemFree(widePath);
    return result;
}
#endif
} // namespace

std::string PickFolderDialog(const wchar_t* title) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(coInit);

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        if (shouldUninitialize) {
            CoUninitialize();
        }
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(title);

    std::string result;
    hr = dialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        hr = dialog->GetResult(&item);
        if (SUCCEEDED(hr) && item != nullptr) {
            result = ShellItemPath(item);
            item->Release();
        }
    }

    dialog->Release();
    if (shouldUninitialize) {
        CoUninitialize();
    }
    return result;
#else
    (void)title;
    return {};
#endif
}

std::string PickImageFileDialog(const wchar_t* title) {
#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) || defined(__CYGWIN__)
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninitialize = SUCCEEDED(coInit);

    IFileDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) {
        if (shouldUninitialize) CoUninitialize();
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    }
    const COMDLG_FILTERSPEC filters[] = {
        {L"Skybox images", L"*.jpg;*.jpeg;*.png;*.bmp;*.tga;*.hdr"},
        {L"All files", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(sizeof(filters) / sizeof(filters[0])), filters);
    dialog->SetFileTypeIndex(1);
    dialog->SetTitle(title);

    std::string result;
    if (SUCCEEDED(dialog->Show(nullptr))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
            result = ShellItemPath(item);
            item->Release();
        }
    }

    dialog->Release();
    if (shouldUninitialize) CoUninitialize();
    return result;
#else
    (void)title;
    return {};
#endif
}

} // namespace raceman
