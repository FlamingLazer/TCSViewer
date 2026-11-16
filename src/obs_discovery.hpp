#pragma once
#include <tlhelp32.h>
#include <filesystem>
#include <vector>
#include <algorithm>

// Never scan an entire drive. A running portable OBS can live anywhere.
inline std::filesystem::path DiscoverObs(const std::filesystem::path& viewerDir) {
 namespace fs = std::filesystem;
 std::vector<fs::path> candidates;
 HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
 if (snapshot != INVALID_HANDLE_VALUE) {
  PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) do {
   if (_wcsicmp(entry.szExeFile, L"obs64.exe") != 0) continue;
   HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
   if (!process) continue;
   wchar_t executable[32768]; DWORD size = 32768;
   if (QueryFullProcessImageNameW(process, 0, executable, &size))
    candidates.push_back(fs::path(executable).parent_path().parent_path().parent_path());
   CloseHandle(process);
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);
 }
 // Support a viewer placed inside, beside, or two levels below portable OBS.
 for (auto location = viewerDir; !location.empty(); location = location.parent_path()) {
  candidates.push_back(location);
  candidates.push_back(location / L"obs-studio");
  if (location == location.root_path() || location == viewerDir.parent_path().parent_path()) break;
 }
 for (HKEY hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
  wchar_t path[32768]; DWORD bytes = sizeof(path);
  if (RegGetValueW(hive, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OBS Studio",
                   L"InstallLocation", RRF_RT_REG_SZ, nullptr, path, &bytes) == ERROR_SUCCESS)
   candidates.emplace_back(path);
 }
 auto environment = [](const wchar_t* name) -> fs::path {
  wchar_t value[32768]; DWORD size = GetEnvironmentVariableW(name, value, 32768);
  return size && size < 32768 ? fs::path(value) : fs::path();
 };
 for (const auto* variable : {L"ProgramW6432", L"ProgramFiles"}) {
  auto path = environment(variable);
  if (!path.empty()) candidates.push_back(path / L"obs-studio");
 }
 auto local = environment(L"LOCALAPPDATA");
 if (!local.empty()) candidates.push_back(local / L"Programs/obs-studio");
 // Portable copies commonly live in Documents or Downloads. Inspect only
 // immediate directories whose names contain OBS, with no recursive search.
 auto profile = environment(L"USERPROFILE");
 if (!profile.empty()) for (const auto* name : {L"Documents", L"Downloads"}) {
  std::error_code error;
  std::vector<fs::path> portable;
  for (fs::directory_iterator it(profile / name, fs::directory_options::skip_permission_denied, error), end;
       !error && it != end; it.increment(error)) {
   auto filename = it->path().filename().wstring();
   std::transform(filename.begin(), filename.end(), filename.begin(), towlower);
   if (filename.find(L"obs") != std::wstring::npos) portable.push_back(it->path());
  }
  std::sort(portable.rbegin(), portable.rend());
  candidates.insert(candidates.end(), portable.begin(), portable.end());
 }
 for (const auto& root : candidates) {
  std::error_code error;
  if (fs::is_regular_file(root / L"bin/64bit/obs.dll", error) &&
      fs::is_regular_file(root / L"bin/64bit/libobs-d3d11.dll", error) &&
      (fs::is_regular_file(root / L"core/win-capture/win-capture.dll", error) ||
       fs::is_regular_file(root / L"obs-plugins/64bit/win-capture.dll", error))) return root;
 }
 throw std::runtime_error("OBS was not found automatically. Start OBS, then toggle the viewer again so its location can be detected.");
}
