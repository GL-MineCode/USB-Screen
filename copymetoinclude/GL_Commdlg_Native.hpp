#ifndef __INC_GL_COMMDLG_NATIVE_
#define __INC_GL_COMMDLG_NATIVE_

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <Shlobj.h>
#include <cstdint>
#include <thread>
#include <memory>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <queue>
#include <mutex>
#include <cmath>
#include "UTF8toWide.hpp"

namespace GLDLG{

    namespace
    {

        /**
         * @brief Construct a file filter string (wide character version)
         * @brief 构建文件过滤器字符串（宽字符版）
         * @param filters Filter list, each element in "description|pattern" format
         * @param filters 过滤器列表，每个元素格式为"描述|过滤模式"
         * @return Wide-character filter string meeting API requirements
         * @return 符合API要求的宽字符过滤器字符串
         * @throw std::invalid_argument Thrown when filter format is invalid
         * @throw std::invalid_argument 过滤器格式错误时抛出
         */
        std::wstring buildFilter(const std::vector<std::string> &filters)
        {
            std::wstring filterStr;

            for (const auto &filter : filters)
            {
                size_t pipePos = filter.find('|');
                if (pipePos == std::string::npos)
                {
                    throw std::invalid_argument(
                        "Invalid filter format: '" + filter +
                        "'. Use '描述|过滤模式' (e.g., 'Text Files(*.txt)|*.txt')");
                }

                std::string desc = filter.substr(0, pipePos);
                std::string pattern = filter.substr(pipePos + 1);
                filterStr += utf8ToWide(desc);
                filterStr += L'\0';
                filterStr += utf8ToWide(pattern);
                filterStr += L'\0';
            }

            filterStr += L'\0';
            return filterStr;
        }

        std::wstring FindFontFileLocalMachine(const std::wstring &fontNameSubstring)
        {
            HKEY hKey;
            LONG result;
            DWORD index = 0;
            WCHAR valueName[256];
            DWORD valueNameSize;
            BYTE valueData[1024];
            DWORD valueDataSize;
            DWORD valueType;

            result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                                   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                   0, KEY_READ, &hKey);

            if (result != ERROR_SUCCESS)
            {
                return L"";
            }

            std::wstring fontPath;

            while (true)
            {
                valueNameSize = sizeof(valueName) / sizeof(WCHAR);
                valueDataSize = sizeof(valueData);

                result = RegEnumValueW(hKey, index, valueName, &valueNameSize,
                                       NULL, &valueType, valueData, &valueDataSize);

                if (result != ERROR_SUCCESS)
                {
                    break;
                }

                index++;

                if (valueType == REG_SZ)
                {
                    std::wstring currentFontName(valueName);
                    std::wstring currentFontPath(reinterpret_cast<wchar_t *>(valueData));

                    size_t pos = currentFontName.find(L" (");
                    if (pos != std::wstring::npos)
                    {
                        currentFontName = currentFontName.substr(0, pos);
                    }

                    std::wstring lowerFontName = currentFontName;
                    std::wstring lowerSubstring = fontNameSubstring;

                    for (auto &c : lowerFontName)
                        c = towlower(c);
                    for (auto &c : lowerSubstring)
                        c = towlower(c);

                    if (lowerFontName.find(lowerSubstring) != std::wstring::npos)
                    {

                        if (currentFontPath.find(L':') == std::wstring::npos)
                        {
                            WCHAR windowsDir[MAX_PATH];
                            GetWindowsDirectoryW(windowsDir, MAX_PATH);
                            fontPath = std::wstring(windowsDir) + L"\\Fonts\\" + currentFontPath;
                        }
                        else
                        {
                            fontPath = currentFontPath;
                        }
                        break;
                    }
                }
            }

            RegCloseKey(hKey);
            return fontPath;
        }

        std::wstring FindFontFileCurrentUser(const std::wstring &fontNameSubstring)
        {
            HKEY hKey;
            LONG result;
            DWORD index = 0;
            WCHAR valueName[256];
            DWORD valueNameSize;
            BYTE valueData[1024];
            DWORD valueDataSize;
            DWORD valueType;

            result = RegOpenKeyExW(HKEY_CURRENT_USER,
                                   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
                                   0, KEY_READ, &hKey);

            if (result != ERROR_SUCCESS)
            {
                return L"";
            }

            std::wstring fontPath;

            while (true)
            {
                valueNameSize = sizeof(valueName) / sizeof(WCHAR);
                valueDataSize = sizeof(valueData);

                result = RegEnumValueW(hKey, index, valueName, &valueNameSize,
                                       NULL, &valueType, valueData, &valueDataSize);

                if (result != ERROR_SUCCESS)
                {
                    break;
                }

                index++;

                if (valueType == REG_SZ)
                {
                    std::wstring currentFontName(valueName);
                    std::wstring currentFontPath(reinterpret_cast<wchar_t *>(valueData));

                    size_t pos = currentFontName.find(L" (");
                    if (pos != std::wstring::npos)
                    {
                        currentFontName = currentFontName.substr(0, pos);
                    }

                    std::wstring lowerFontName = currentFontName;
                    std::wstring lowerSubstring = fontNameSubstring;

                    for (auto &c : lowerFontName)
                        c = towlower(c);
                    for (auto &c : lowerSubstring)
                        c = towlower(c);

                    if (lowerFontName.find(lowerSubstring) != std::wstring::npos)
                    {

                        if (currentFontPath.find(L':') == std::wstring::npos)
                        {
                            WCHAR windowsDir[MAX_PATH];
                            GetWindowsDirectoryW(windowsDir, MAX_PATH);
                            fontPath = std::wstring(windowsDir) + L"\\Fonts\\" + currentFontPath;
                        }
                        else
                        {
                            fontPath = currentFontPath;
                        }
                        break;
                    }
                }
            }

            RegCloseKey(hKey);
            return fontPath;
        }

        std::wstring FindFontFile(const std::wstring &fontNameSubstring)
        {
            std::wstring try_lm = FindFontFileLocalMachine(fontNameSubstring);
            if (try_lm.empty())
            {
                try_lm = FindFontFileCurrentUser(fontNameSubstring);
            }
            return try_lm;
        }

        bool IsVistaOrNewer()
        {
            OSVERSIONINFOEX os = {0};
            os.dwOSVersionInfoSize = sizeof(os);
            GetVersionEx((OSVERSIONINFO *)&os);
            return os.dwMajorVersion >= 6;
        }

    }

    /**
     * @brief Show the file open dialog, allowing the user to select an existing file
     * @brief 显示文件打开对话框，让用户选择一个已存在的文件
     * @param filters List of file filters, each must follow "description|pattern" format:
     *                - Description: text displayed in the filter dropdown (e.g. "Text Files(*.txt)")
     *                - Pattern: file matching rules (e.g. "*.txt", multiple patterns separated by ";" like "*.bmp;*.jpg")
     *                Example: {"Text Files(*.txt)|*.txt", "All Files(*.*)|*.*"}
     * @param filters 文件过滤器列表，每个元素必须遵循"描述|过滤模式"格式：
     *                - 描述：显示在过滤器下拉框中的文本（如"文本文件(*.txt)"）
     *                - 过滤模式：文件匹配规则（如"*.txt"，多模式用分号分隔"*.bmp;*.jpg"）
     *                示例：{"文本文件(*.txt)|*.txt", "所有文件(*.*)|*.*"}
     * @param title Window title of the file open dialog; if empty, defaults to "Open"
     * @param title 文件打开对话框的窗口标题，若为空则使用默认的标题，即"打开"
     * @param initialDir Initial directory (UTF-8); if empty, uses current working directory
     * @param initialDir 初始目录（UTF8编码），为空则使用当前工作目录
     * @param defaultFileName Default file name displayed (UTF-8); if empty, not set
     * @param defaultFileName 默认显示的文件名（UTF8编码），为空则不设置
     * @param defaultExt Default extension (without dot, e.g. "txt"); auto-appended if user omits extension
     * @param defaultExt 默认扩展名（无需带点，如"txt"），用户未输入扩展名时自动添加
     * @param parentHWND Parent window handle of the file open dialog
     * @param parentHWND 文件打开对话框的父窗口句柄
     * @return Selected file path (UTF-8); returns empty string if user cancels
     * @return 选中的文件路径（UTF8编码），用户取消时返回空字符串
     * @throw std::invalid_argument When filter format is invalid
     * @throw std::invalid_argument 过滤器格式错误时
     * @throw std::runtime_error When string conversion or dialog call fails
     * @throw std::runtime_error 字符串转换失败或对话框调用出错时
     */
    std::string getOpenFileName(const std::vector<std::string> &filters,
                                const std::string &title = "",
                                const std::string &initialDir = "",
                                const std::string &defaultFileName = "",
                                const std::string &defaultExt = "", HWND parentHWND = NULL)
    {
        std::wstring filter = buildFilter(filters);
        std::wstring wtitle = utf8ToWide(title);

        const int MAX_PATH_LEN = 4096;
        std::wstring filePath(MAX_PATH_LEN, L'\0');
        if (!defaultFileName.empty())
        {
            std::wstring defaultWide = utf8ToWide(defaultFileName);
            if (defaultWide.size() >= MAX_PATH_LEN)
            {
                throw std::runtime_error("Default file name is too long");
            }
            wcscpy_s(&filePath[0], MAX_PATH_LEN, defaultWide.c_str());
        }

        std::wstring initialDirWide;
        LPCWSTR initialDirPtr = nullptr;
        if (!initialDir.empty())
        {
            initialDirWide = utf8ToWide(initialDir);
            initialDirPtr = initialDirWide.c_str();
        }

        std::wstring defaultExtWide;
        LPCWSTR defaultExtPtr = nullptr;
        if (!defaultExt.empty())
        {
            defaultExtWide = utf8ToWide(defaultExt);
            defaultExtPtr = defaultExtWide.c_str();
        }

        OPENFILENAMEW ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = parentHWND;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = &filePath[0];
        ofn.nMaxFile = MAX_PATH_LEN;
        ofn.lpstrInitialDir = initialDirPtr;
        ofn.lpstrDefExt = defaultExtPtr;
        if (!title.empty())
            ofn.lpstrTitle = wtitle.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST |
                    OFN_PATHMUSTEXIST |
                    OFN_NOCHANGEDIR |
                    OFN_EXPLORER;

        if (!GetOpenFileNameW(&ofn))
        {
            DWORD err = CommDlgExtendedError();
            if (err != 0)
            {
                throw std::runtime_error("Open file dialog failed: " + std::to_string(err));
            }
            return "";
        }

        filePath.resize(wcslen(filePath.c_str()));
        return wideToUtf8(filePath);
    }

    /**
     * @brief Show the file save dialog, allowing the user to specify a save path
     * @brief 显示文件保存对话框，让用户指定文件保存路径
     * @param filters List of file filters, each must follow "description|pattern" format:
     *                - Description: text displayed in the filter dropdown (e.g. "Text Files(*.txt)")
     *                - Pattern: file matching rules (e.g. "*.txt", multiple patterns separated by ";")
     *                Example: {"Text Files(*.txt)|*.txt", "All Files(*.*)|*.*"}
     * @param filters 文件过滤器列表，每个元素必须遵循"描述|过滤模式"格式：
     *                - 描述：显示在过滤器下拉框中的文本（如"文本文件(*.txt)"）
     *                - 过滤模式：文件匹配规则（如"*.txt"，多模式用分号分隔"*.bmp;*.jpg"）
     *                示例：{"文本文件(*.txt)|*.txt", "所有文件(*.*)|*.*"}
     * @param title Window title of the file save dialog; if empty, defaults to "Save As"
     * @param title 文件打开对话框的窗口标题，若为空则使用默认的标题，即"另存为"
     * @param initialDir Initial directory (UTF-8); if empty, uses current working directory
     * @param initialDir 初始目录（UTF8编码），为空则使用当前工作目录
     * @param defaultFileName Default file name displayed (UTF-8); if empty, not set
     * @param defaultFileName 默认显示的文件名（UTF8编码），为空则不设置
     * @param defaultExt Default extension (without dot, e.g. "txt"); auto-appended if user omits extension
     * @param defaultExt 默认扩展名（无需带点，如"txt"），用户未输入扩展名时自动添加
     * @param parentHWND Parent window handle of the file save dialog
     * @param parentHWND 文件保存对话框的父窗口句柄
     * @return Selected save path (UTF-8); returns empty string if user cancels
     * @return 选中的文件保存路径（UTF8编码），用户取消时返回空字符串
     * @throw std::invalid_argument When filter format is invalid
     * @throw std::invalid_argument 过滤器格式错误时
     * @throw std::runtime_error When string conversion or dialog call fails
     * @throw std::runtime_error 字符串转换失败或对话框调用出错时
     */
    std::string getSaveFileName(const std::vector<std::string> &filters,
                                const std::string &title = "",
                                const std::string &initialDir = "",
                                const std::string &defaultFileName = "",
                                const std::string &defaultExt = "", HWND parentHWND = NULL)
    {
        std::wstring filter = buildFilter(filters);
        std::wstring wtitle = utf8ToWide(title);

        const int MAX_PATH_LEN = 4096;
        std::wstring filePath(MAX_PATH_LEN, L'\0');
        if (!defaultFileName.empty())
        {
            std::wstring defaultWide = utf8ToWide(defaultFileName);
            if (defaultWide.size() >= MAX_PATH_LEN)
            {
                throw std::runtime_error("Default file name is too long");
            }
            wcscpy_s(&filePath[0], MAX_PATH_LEN, defaultWide.c_str());
        }

        std::wstring initialDirWide;
        LPCWSTR initialDirPtr = nullptr;
        if (!initialDir.empty())
        {
            initialDirWide = utf8ToWide(initialDir);
            initialDirPtr = initialDirWide.c_str();
        }

        std::wstring defaultExtWide;
        LPCWSTR defaultExtPtr = nullptr;
        if (!defaultExt.empty())
        {
            defaultExtWide = utf8ToWide(defaultExt);
            defaultExtPtr = defaultExtWide.c_str();
        }

        OPENFILENAMEW ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = parentHWND;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = &filePath[0];
        ofn.nMaxFile = MAX_PATH_LEN;
        ofn.lpstrInitialDir = initialDirPtr;
        ofn.lpstrDefExt = defaultExtPtr;
        if (!title.empty())
            ofn.lpstrTitle = wtitle.c_str();
        ofn.Flags = OFN_OVERWRITEPROMPT |
                    OFN_PATHMUSTEXIST |
                    OFN_NOCHANGEDIR |
                    OFN_EXPLORER;

        if (!GetSaveFileNameW(&ofn))
        {
            DWORD err = CommDlgExtendedError();
            if (err != 0)
            {
                throw std::runtime_error("Save file dialog failed: " + std::to_string(err));
            }
            return "";
        }

        filePath.resize(wcslen(filePath.c_str()));
        return wideToUtf8(filePath);
    }

    /**
     * @brief Show the file open dialog, allowing the user to select multiple existing files
     * @brief 显示文件打开对话框，让用户选择多个已存在的文件
     * @param filters List of file filters, each must follow "description|pattern" format:
     *                - Description: text displayed in the filter dropdown (e.g. "Text Files(*.txt)")
     *                - Pattern: file matching rules (e.g. "*.txt", multiple patterns separated by ";")
     *                Example: {"Text Files(*.txt)|*.txt", "All Files(*.*)|*.*"}
     * @param filters 文件过滤器列表，每个元素必须遵循"描述|过滤模式"格式：
     *                - 描述：显示在过滤器下拉框中的文本（如"文本文件(*.txt)"）
     *                - 过滤模式：文件匹配规则（如"*.txt"，多模式用分号分隔"*.bmp;*.jpg"）
     *                示例：{"文本文件(*.txt)|*.txt", "所有文件(*.*)|*.*"}
     * @param title Window title of the file open dialog; if empty, defaults to "Open"
     * @param title 文件打开对话框的窗口标题，若为空则使用默认的标题，即"打开"
     * @param initialDir Initial directory (UTF-8); if empty, uses current working directory
     * @param initialDir 初始目录（UTF8编码），为空则使用当前工作目录
     * @param defaultFileName Default file name displayed (UTF-8); if empty, not set
     * @param defaultFileName 默认显示的文件名（UTF8编码），为空则不设置
     * @param defaultExt Default extension (without dot, e.g. "txt"); auto-appended if user omits extension
     * @param defaultExt 默认扩展名（无需带点，如"txt"），用户未输入扩展名时自动添加
     * @param parentHWND Parent window handle of the file open dialog
     * @param parentHWND 文件打开对话框的父窗口句柄
     * @return List of selected file paths (UTF-8); returns empty vector if user cancels
     * @return 选中的文件路径列表（UTF8编码），用户取消时返回空vector
     * @throw std::invalid_argument When filter format is invalid
     * @throw std::invalid_argument 过滤器格式错误时
     * @throw std::runtime_error When string conversion or dialog call fails
     * @throw std::runtime_error 字符串转换失败或对话框调用出错时
     */
    std::vector<std::string> getOpenMultipleFileNames(const std::vector<std::string> &filters,
                                                      const std::string &title = "",
                                                      const std::string &initialDir = "",
                                                      const std::string &defaultFileName = "",
                                                      const std::string &defaultExt = "",
                                                      HWND parentHWND = NULL)
    {
        std::wstring filter = buildFilter(filters);
        std::wstring wtitle = utf8ToWide(title);

        // 为多选文件分配更大的缓冲区（64KB）
        const int BUFFER_SIZE = 65536;
        std::vector<wchar_t> filePathBuffer(BUFFER_SIZE, L'\0');

        if (!defaultFileName.empty())
        {
            std::wstring defaultWide = utf8ToWide(defaultFileName);
            if (defaultWide.size() * sizeof(wchar_t) >= BUFFER_SIZE)
            {
                throw std::runtime_error("Default file name is too long");
            }
            wcscpy_s(filePathBuffer.data(), BUFFER_SIZE / sizeof(wchar_t), defaultWide.c_str());
        }

        std::wstring initialDirWide;
        LPCWSTR initialDirPtr = nullptr;
        if (!initialDir.empty())
        {
            initialDirWide = utf8ToWide(initialDir);
            initialDirPtr = initialDirWide.c_str();
        }

        std::wstring defaultExtWide;
        LPCWSTR defaultExtPtr = nullptr;
        if (!defaultExt.empty())
        {
            defaultExtWide = utf8ToWide(defaultExt);
            defaultExtPtr = defaultExtWide.c_str();
        }

        OPENFILENAMEW ofn = {0};
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = parentHWND;
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = filePathBuffer.data();
        ofn.nMaxFile = BUFFER_SIZE;
        ofn.lpstrInitialDir = initialDirPtr;
        ofn.lpstrDefExt = defaultExtPtr;
        if (!title.empty())
            ofn.lpstrTitle = wtitle.c_str();
        ofn.Flags = OFN_FILEMUSTEXIST |
                    OFN_PATHMUSTEXIST |
                    OFN_NOCHANGEDIR |
                    OFN_EXPLORER |
                    OFN_ALLOWMULTISELECT;

        if (!GetOpenFileNameW(&ofn))
        {
            DWORD err = CommDlgExtendedError();
            if (err != 0)
            {
                throw std::runtime_error("Open file dialog failed: " + std::to_string(err));
            }
            return {};
        }

        std::vector<std::string> selectedFiles;

        const wchar_t *ptr = filePathBuffer.data();
        std::wstring directory = ptr;
        ptr += directory.length() + 1;

        if (*ptr == L'\0')
        {
            selectedFiles.push_back(wideToUtf8(directory));
        }
        else
        {
            while (*ptr != L'\0')
            {
                std::wstring filename = ptr;
                std::wstring fullPath = directory + L"\\" + filename;
                selectedFiles.push_back(wideToUtf8(fullPath));
                ptr += filename.length() + 1;
            }
        }

        return selectedFiles;
    }

    namespace
    {
        std::string __getOpenDirectoryName_BelowVista(const std::string &title, const std::string &initialDir, HWND parentHWND)
        {
            std::wstring wtitle = utf8ToWide(title);

            std::wstring initialDirWide;
            if (!initialDir.empty())
            {
                initialDirWide = utf8ToWide(initialDir);
            }

            BROWSEINFOW bi = {0};
            bi.hwndOwner = parentHWND;
            if (!title.empty())
                bi.lpszTitle = wtitle.c_str();
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

            if (!initialDirWide.empty())
            {
                bi.lParam = reinterpret_cast<LPARAM>(initialDirWide.c_str());
                bi.lpfn = [](HWND hwnd, UINT uMsg, LPARAM /*lParam*/, LPARAM lpData) -> int
                {
                    if (uMsg == BFFM_INITIALIZED)
                    {
                        SendMessageW(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
                    }
                    return 0;
                };
            }

            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl == nullptr)
            {
                return "";
            }

            std::wstring directoryPath(MAX_PATH, L'\0');
            if (!SHGetPathFromIDListW(pidl, &directoryPath[0]))
            {
                CoTaskMemFree(pidl);
                throw std::runtime_error("Failed to get path from ID list");
            }

            CoTaskMemFree(pidl);
            directoryPath.resize(wcslen(directoryPath.c_str()));
            return wideToUtf8(directoryPath);
        }

        std::string __getOpenDirectoryName_VistaOrNewer(const std::string &title, const std::string &initialDir, HWND parentHWND)
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr))
                throw std::runtime_error("COM initialize failed");

            IFileOpenDialog *pFolderDlg = nullptr;
            hr = CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_ALL,
                IID_IFileOpenDialog,
                reinterpret_cast<void **>(&pFolderDlg));
            if (FAILED(hr))
            {
                CoUninitialize();
                throw std::runtime_error("Create IFileOpenDialog failed");
            }

            DWORD opt = 0;
            pFolderDlg->GetOptions(&opt);
            opt |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM;
            pFolderDlg->SetOptions(opt);

            if (!title.empty())
            {
                std::wstring wTitle = utf8ToWide(title);
                pFolderDlg->SetTitle(wTitle.c_str());
            }

            if (!initialDir.empty())
            {
                std::wstring wInitDir = utf8ToWide(initialDir);
                IShellItem *pInitItem = nullptr;
                hr = SHCreateItemFromParsingName(wInitDir.c_str(), nullptr, IID_IShellItem, reinterpret_cast<void **>(&pInitItem));
                if (SUCCEEDED(hr))
                {
                    pFolderDlg->SetFolder(pInitItem);
                    pInitItem->Release();
                }
            }

            std::string result;
            hr = pFolderDlg->Show(parentHWND);
            if (SUCCEEDED(hr))
            {
                IShellItem *pSelItem = nullptr;
                hr = pFolderDlg->GetResult(&pSelItem);
                if (SUCCEEDED(hr))
                {
                    PWSTR pWidePath = nullptr;
                    hr = pSelItem->GetDisplayName(SIGDN_FILESYSPATH, &pWidePath);
                    if (SUCCEEDED(hr))
                    {
                        result = wideToUtf8(pWidePath);
                        CoTaskMemFree(pWidePath);
                    }
                    pSelItem->Release();
                }
            }

            pFolderDlg->Release();
            CoUninitialize();

            return result;
        }

        std::vector<std::string> __getOpenDirectoryNames_BelowVista(
            const std::string &title, const std::string &initialDir, HWND parentHWND)
        {
            // SHBrowseForFolderW does not support multi-select,
            // so fall back to single-select mode.
            std::string single = __getOpenDirectoryName_BelowVista(title, initialDir, parentHWND);
            if (single.empty())
                return {};
            return {single};
        }

        std::vector<std::string> __getOpenDirectoryNames_VistaOrNewer(
            const std::string &title, const std::string &initialDir, HWND parentHWND)
        {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(hr))
                throw std::runtime_error("COM initialize failed");

            IFileOpenDialog *pFolderDlg = nullptr;
            hr = CoCreateInstance(
                CLSID_FileOpenDialog,
                nullptr,
                CLSCTX_ALL,
                IID_IFileOpenDialog,
                reinterpret_cast<void **>(&pFolderDlg));
            if (FAILED(hr))
            {
                CoUninitialize();
                throw std::runtime_error("Create IFileOpenDialog failed");
            }

            DWORD opt = 0;
            pFolderDlg->GetOptions(&opt);
            opt |= FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_ALLOWMULTISELECT;
            pFolderDlg->SetOptions(opt);

            if (!title.empty())
            {
                std::wstring wTitle = utf8ToWide(title);
                pFolderDlg->SetTitle(wTitle.c_str());
            }

            if (!initialDir.empty())
            {
                std::wstring wInitDir = utf8ToWide(initialDir);
                IShellItem *pInitItem = nullptr;
                hr = SHCreateItemFromParsingName(wInitDir.c_str(), nullptr, IID_IShellItem,
                                                 reinterpret_cast<void **>(&pInitItem));
                if (SUCCEEDED(hr))
                {
                    pFolderDlg->SetFolder(pInitItem);
                    pInitItem->Release();
                }
            }

            std::vector<std::string> results;
            hr = pFolderDlg->Show(parentHWND);
            if (SUCCEEDED(hr))
            {
                IShellItemArray *pItemsArray = nullptr;
                hr = pFolderDlg->GetResults(&pItemsArray);
                if (SUCCEEDED(hr) && pItemsArray)
                {
                    DWORD count = 0;
                    pItemsArray->GetCount(&count);
                    results.reserve(count);
                    for (DWORD i = 0; i < count; ++i)
                    {
                        IShellItem *pItem = nullptr;
                        hr = pItemsArray->GetItemAt(i, &pItem);
                        if (SUCCEEDED(hr) && pItem)
                        {
                            PWSTR pWidePath = nullptr;
                            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pWidePath);
                            if (SUCCEEDED(hr) && pWidePath)
                            {
                                results.push_back(wideToUtf8(pWidePath));
                                CoTaskMemFree(pWidePath);
                            }
                            pItem->Release();
                        }
                    }
                    pItemsArray->Release();
                }
            }

            pFolderDlg->Release();
            CoUninitialize();

            return results;
        }
    }

    /**
     * @brief Show the directory selection dialog, allowing the user to select a folder
     * @brief 显示目录选择对话框，让用户选择一个目录
     * @param title Prompt text displayed in the directory selection dialog
     * @param title 目录选择对话框中显示的提示文字
     * @param initialDir Initial directory (UTF-8); if empty, uses current working directory
     * @param initialDir 初始目录（UTF8编码），为空则使用当前工作目录
     * @param parentHWND Parent window handle of the directory selection dialog
     * @param parentHWND 目录选择对话框的父窗口句柄
     * @return Selected directory path (UTF-8); returns empty string if user cancels
     * @return 选中的目录路径（UTF8编码），用户取消时返回空字符串
     * @throw std::runtime_error When string conversion or dialog call fails
     * @throw std::runtime_error 字符串转换失败或对话框调用出错时
     *
     * @note On systems below Vista,since SHBrowseForFolderW does not support custom window titles, the "title" parameter here is actually prompt text, not a real window title. The default window title of SHBrowseForFolderW is "Browse For Folder".
     * @note 在Vista以下的系统上，由于SHBrowseForFolderW不支持自定义窗口标题，所以参数title指的不算是真正意义上的"标题"，应该算提示文字。另外SHBrowseForFolderW默认的窗口标题是"浏览文件夹"
     */
    std::string getOpenDirectoryName(const std::string &title = "", const std::string &initialDir = "", HWND parentHWND = NULL)
    {
        if (IsVistaOrNewer())
        {
            return __getOpenDirectoryName_VistaOrNewer(title, initialDir, parentHWND);
        }
        else
        {
            return __getOpenDirectoryName_BelowVista(title, initialDir, parentHWND);
        }
    }

    /**
     * @brief Show the directory selection dialog, allowing the user to select one or more folders
     * @brief 显示目录选择对话框，让用户选择一个或多个目录
     * @param title Prompt text displayed in the directory selection dialog
     * @param title 目录选择对话框中显示的提示文字
     * @param initialDir Initial directory (UTF-8); if empty, uses current working directory
     * @param initialDir 初始目录（UTF8编码），为空则使用当前工作目录
     * @param parentHWND Parent window handle of the directory selection dialog
     * @param parentHWND 目录选择对话框的父窗口句柄
     * @return List of selected directory paths (UTF-8); empty vector if cancelled
     * @return 选中的目录路径列表（UTF8编码），用户取消时返回空vector
     * @throw std::runtime_error When string conversion or dialog call fails
     * @throw std::runtime_error 字符串转换失败或对话框调用出错时
     * @note On Vista+, IFileOpenDialog with FOS_ALLOWMULTISELECT is used. Below Vista, SHBrowseForFolderW does not support multi-select, so a single-select fallback is used (returns 0 or 1 path).
     * @note 在Vista及以上系统使用IFileOpenDialog的FOS_ALLOWMULTISELECT实现多选。Vista以下系统因SHBrowseForFolderW不支持多选，退化到单选模式（返回0或1个路径）。
     */
    std::vector<std::string> getOpenDirectoryNames(const std::string &title = "", const std::string &initialDir = "", HWND parentHWND = NULL)
    {
        if (IsVistaOrNewer())
        {
            return __getOpenDirectoryNames_VistaOrNewer(title, initialDir, parentHWND);
        }
        else
        {
            return __getOpenDirectoryNames_BelowVista(title, initialDir, parentHWND);
        }
    }

    struct chooseFontInfo
    {
        std::string fontFaceName;
        std::string fontPath;
        int fontPointSize;
    };

    /**
     * @brief Show the font selection dialog, allowing the user to pick a font installed on the system
     * @brief 显示字体选择对话框，让用户选择系统上所安装的字体
     *
     * @param cfi Output parameter. Note: the path of the selected font can usually be found, but is not guaranteed. If not found, fontPath will be an empty string.
     * @param cfi 输出参数，注意，不保证一定可以找到选择的字体的路径，但大概率可以找到。若没找到，fontPath为空字符串。
     * @param hwndParent Parent window handle of the font selection dialog
     * @param hwndParent 颜色选择对话框的父窗口句柄
     */
    void chooseFont(chooseFontInfo &cfi, HWND hwndParent = NULL)
    {
        CHOOSEFONTW cf = {0};
        LOGFONTW lf = {0};
        cf.lStructSize = sizeof(CHOOSEFONTW);
        cf.hwndOwner = hwndParent;
        cf.lpLogFont = &lf;
        cf.Flags = CF_SCREENFONTS | CF_NOVERTFONTS | CF_TTONLY;
        if (!ChooseFontW(&cf))
        {
            DWORD err = CommDlgExtendedError();
            if (err != 0)
            {
                throw std::runtime_error("Choose font dialog failed: " + std::to_string(err));
            }
        }
        cfi.fontFaceName = wideToUtf8(lf.lfFaceName);
        cfi.fontPointSize = cf.iPointSize / 10;
        cfi.fontPath = wideToUtf8(FindFontFile(lf.lfFaceName));
    }
}

#endif