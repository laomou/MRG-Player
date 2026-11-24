#include "platform/RomFilePicker.hpp"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

#include "game/GamePlatform.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace launcher::platform {
namespace {

#if defined(_WIN32)

std::wstring widen(const std::string &text) {
  if (text.empty())
    return std::wstring();
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), length);
  return wide;
}

std::string narrow(const std::wstring &text) {
  if (text.empty())
    return std::string();
  const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr,
                                         nullptr);
  std::string narrowText(static_cast<std::size_t>(length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), narrowText.data(), length, nullptr,
                      nullptr);
  return narrowText;
}

// The Win32 common dialog wants its filter as a run of doubled-null terminated
// strings; SDL2 has no file dialog of its own, so the launcher asks Windows.
std::wstring gameFileFilter() {
  std::wstring patterns;
  for (const std::string &extension : game::gameExtensions()) {
    if (!patterns.empty())
      patterns += L';';
    patterns += L"*.";
    patterns += widen(extension);
  }
  std::wstring filter = L"Game files";
  filter.push_back(L'\0');
  filter += patterns;
  filter.push_back(L'\0');
  filter += L"All files";
  filter.push_back(L'\0');
  filter += L"*.*";
  filter.push_back(L'\0');
  filter.push_back(L'\0');
  return filter;
}

std::optional<std::string> pickGameFileWindows(std::string &error) {
  wchar_t path[4096] = {};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  const std::wstring filter = gameFileFilter();
  dialog.lpstrFilter = filter.c_str();
  dialog.lpstrFile = path;
  dialog.nMaxFile = static_cast<DWORD>(std::size(path));
  dialog.lpstrTitle = L"Import Game";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (GetOpenFileNameW(&dialog) == FALSE) {
    const DWORD code = CommDlgExtendedError();
    if (code == 0)
      return std::nullopt;
    error = "The file dialog failed (error " + std::to_string(code) + ")";
    return std::nullopt;
  }
  return narrow(path);
}

#elif defined(__linux__)

// The extensions the library imports, in the "*.nes *.gba" form zenity wants.
std::string extensionFilter() {
  std::string filter;
  for (const std::string &extension : game::gameExtensions()) {
    if (!filter.empty())
      filter += ' ';
    filter += "*." + extension;
  }
  return filter;
}

#endif

} // namespace

std::optional<std::string> pickGameFile(std::string &error) {
  error.clear();
#if defined(_WIN32)
  return pickGameFileWindows(error);
#elif defined(__linux__)
  if (std::system("command -v zenity >/dev/null 2>&1") != 0) {
    error = "zenity is unavailable; drag a game file into the window";
    return std::nullopt;
  }
  const std::string command = "zenity --file-selection --title='Import Game' --file-filter='Game files | " +
                              extensionFilter() + "' 2>/dev/null";
  FILE *pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    error = "Cannot open the game file picker";
    return std::nullopt;
  }
  std::string path;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr)
    path += buffer;
  const int status = pclose(pipe);
  while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
    path.pop_back();
  if (status != 0 || path.empty())
    return std::nullopt;
  return path;
#else
  error = "Native file picker is not available on this platform; drag a game file into the window";
  return std::nullopt;
#endif
}

} // namespace launcher::platform
