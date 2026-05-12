#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>
#include <vector>

// Windows resolves a program's directly imported DLLs before main() runs. That
// means an app cannot call AddDllDirectory() from its own main() to find Qt,
// Protobuf, libusb, SQLite, or VC runtime DLLs stored in a sibling lib
// directory. This tiny launcher keeps the public install directory tidy while
// still using normal Windows DLL loading:
//
//   tag-tools/
//     qtmonitor.exe        launcher, same name users run
//     apps/qtmonitor.exe   real program with normal DLL imports
//     lib/*.dll            Qt, vcpkg, and runtime DLLs
//
// The launcher prepends tag-tools/lib to PATH in the child process environment
// and starts the matching executable from tag-tools/apps.

namespace {

// Reconstruct a command line for CreateProcessW using the quoting rules
// documented for CommandLineToArgvW-compatible argv parsing. We need this
// because CreateProcessW takes one mutable command-line string, not argc/argv.
std::wstring quoteArgument(const std::wstring &arg)
{
  if (arg.empty())
    return L"\"\"";

  bool needsQuotes = false;
  for (wchar_t ch : arg) {
    if (ch == L' ' || ch == L'\t' || ch == L'"') {
      needsQuotes = true;
      break;
    }
  }
  if (!needsQuotes)
    return arg;

  std::wstring quoted = L"\"";
  unsigned int backslashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++backslashes;
    } else if (ch == L'"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(ch);
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      quoted.push_back(ch);
      backslashes = 0;
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

std::wstring directoryName(const std::wstring &path)
{
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return L".";
  return path.substr(0, slash);
}

std::wstring fileName(const std::wstring &path)
{
  const size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos)
    return path;
  return path.substr(slash + 1);
}

std::wstring lastErrorMessage(DWORD error)
{
  wchar_t *buffer = nullptr;
  const DWORD length = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, error, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
  std::wstring message =
      length > 0 ? std::wstring(buffer, length) : L"Unknown Windows error";
  if (buffer != nullptr)
    LocalFree(buffer);
  return message;
}

void showError(const std::wstring &message)
{
#ifdef TAG_TOOLS_GUI_LAUNCHER
  MessageBoxW(nullptr, message.c_str(), L"Tag Tools Launcher",
              MB_OK | MB_ICONERROR);
#else
  std::fwprintf(stderr, L"%ls\n", message.c_str());
#endif
}

int runLauncher()
{
  // GetModuleFileNameW truncates when the buffer is too small. Grow the buffer
  // until we have the complete launcher path.
  std::vector<wchar_t> modulePath(MAX_PATH);
  DWORD pathLength = 0;
  for (;;) {
    pathLength = GetModuleFileNameW(
        nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
    if (pathLength == 0) {
      showError(L"Unable to locate launcher executable.");
      return 1;
    }
    if (pathLength < modulePath.size() - 1)
      break;
    modulePath.resize(modulePath.size() * 2);
  }

  const std::wstring launcherPath(modulePath.data(), pathLength);
  const std::wstring rootDir = directoryName(launcherPath);
  const std::wstring appName = fileName(launcherPath);
  const std::wstring appPath = rootDir + L"\\apps\\" + appName;
  const std::wstring libPath = rootDir + L"\\lib";

  // Prepend the package-local DLL directory for the child process. PATH is used
  // instead of AddDllDirectory because the real program's imports are resolved
  // before its main() can run.
  DWORD pathSize = GetEnvironmentVariableW(L"PATH", nullptr, 0);
  std::wstring newPath = libPath;
  if (pathSize > 0) {
    std::vector<wchar_t> oldPath(pathSize);
    GetEnvironmentVariableW(L"PATH", oldPath.data(), pathSize);
    newPath += L";";
    newPath += oldPath.data();
  }
  SetEnvironmentVariableW(L"PATH", newPath.c_str());

  // Preserve user arguments exactly, replacing only argv[0] with the real
  // executable path. This keeps console tools behaving as if the user launched
  // apps/<tool>.exe directly.
  int argc = 0;
  wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    showError(L"Unable to parse launcher command line.");
    return 1;
  }

  std::wstring commandLine = quoteArgument(appPath);
  for (int i = 1; i < argc; ++i) {
    commandLine.push_back(L' ');
    commandLine += quoteArgument(argv[i]);
  }
  LocalFree(argv);

  // CreateProcessW may modify its command-line buffer, so pass mutable storage.
  std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
  commandLineBuffer.push_back(L'\0');

  STARTUPINFOW startupInfo{};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo{};
  if (!CreateProcessW(appPath.c_str(), commandLineBuffer.data(), nullptr,
                      nullptr, TRUE, 0, nullptr, nullptr, &startupInfo,
                      &processInfo)) {
    showError(L"Unable to launch " + appPath + L": " +
              lastErrorMessage(GetLastError()));
    return 1;
  }

  WaitForSingleObject(processInfo.hProcess, INFINITE);

  DWORD exitCode = 1;
  GetExitCodeProcess(processInfo.hProcess, &exitCode);
  CloseHandle(processInfo.hThread);
  CloseHandle(processInfo.hProcess);
  return static_cast<int>(exitCode);
}

} // namespace

#ifdef TAG_TOOLS_GUI_LAUNCHER
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
  return runLauncher();
}
#else
int wmain()
{
  return runLauncher();
}
#endif
