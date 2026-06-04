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
            PWSTR widePath = nullptr;
            hr = item->GetDisplayName(SIGDN_FILESYSPATH, &widePath);
            if (SUCCEEDED(hr) && widePath != nullptr) {
                const int size = WideCharToMultiByte(CP_UTF8, 0, widePath, -1, nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    result.resize(static_cast<size_t>(size - 1));
                    WideCharToMultiByte(CP_UTF8, 0, widePath, -1, result.data(), size, nullptr, nullptr);
                }
                CoTaskMemFree(widePath);
            }
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

} // namespace raceman
