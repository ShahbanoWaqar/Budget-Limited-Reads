// Windows launcher for mirrored reads on separate D: and E: HDDs.
// Every run gets new directories; no existing file is overwritten or deleted.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
namespace {
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
};
struct Config {
  uint64_t size_mib = 64, rate = 100, duration = 10, delay_us = 5000;
  uint64_t permits = 2, threshold = 8, cap = 32;
  fs::path output;
  bool pause = true, help = false;
};
std::runtime_error win_error(const char* action) {
  auto code = GetLastError();
  return std::runtime_error(std::string(action) + ": " +
    std::system_category().message(static_cast<int>(code)) + " (" + std::to_string(code) + ")");
}
std::wstring quote(const std::wstring& text) {
  // CommandLineToArgvW/CRT-compatible quoting, including trailing backslashes.
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t c : text) {
    if (c == L'\\') { ++slashes; continue; }
    if (c == L'"') { result.append(slashes * 2 + 1, L'\\'); result += c; }
    else { result.append(slashes, L'\\'); result += c; }
    slashes = 0;
  }
  result.append(slashes * 2, L'\\'); result += L'"';
  return result;
}
Config parse(int argc, char** argv) {
  Config config;
  std::map<std::string, uint64_t*> numbers = {
    {"--size-mib", &config.size_mib}, {"--rate", &config.rate},
    {"--duration", &config.duration}, {"--delay-us", &config.delay_us},
    {"--permits", &config.permits}, {"--threshold", &config.threshold},
    {"--max-outstanding", &config.cap}
  };
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    if (key == "--no-pause") { config.pause = false; continue; }
    if (key == "--help") { config.help = true; config.pause = false; continue; }
    if (key == "--output-dir") {
      if (++i == argc || !*argv[i]) throw std::runtime_error("--output-dir requires a path");
      config.output = fs::absolute(fs::path(argv[i])); continue;
    }
    auto it = numbers.find(key);
    if (it == numbers.end() || ++i == argc) throw std::runtime_error("unknown or incomplete option: " + key);
    std::string value = argv[i];
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
      throw std::runtime_error("invalid integer for " + key);
    *it->second = std::stoull(value);
  }
  if (!config.size_mib || config.size_mib > 1048576 || !config.rate || config.rate > 1000000000 ||
      !config.duration || config.duration > 10000000 || config.rate * config.duration > 10000000 ||
      !config.cap || config.cap > 65536 || config.delay_us > 1000000000 ||
      config.permits > 1000000000 || config.threshold > 1000000000)
    throw std::runtime_error("invalid run size/rate/duration/cap (maximum 10 million trace entries)");
  return config;
}
fs::path executable_dir() {
  std::vector<wchar_t> path(32768);
  DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!n || n >= path.size()) throw win_error("locate executable");
  return fs::path(std::wstring(path.data(), n)).parent_path();
}
fs::path default_output() {
  std::vector<wchar_t> path(32768);
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path.data(), static_cast<DWORD>(path.size()));
  if (!n || n >= path.size()) throw std::runtime_error("LOCALAPPDATA unavailable; specify --output-dir PATH");
  return fs::path(std::wstring(path.data(), n)) / L"BudgetLimitedReads" / L"results";
}
std::string run_id() {
  SYSTEMTIME time{}; GetLocalTime(&time);
  std::ostringstream out;
  out << std::setfill('0') << time.wYear << '-' << std::setw(2) << time.wMonth << '-'
      << std::setw(2) << time.wDay << '_' << std::setw(2) << time.wHour
      << std::setw(2) << time.wMinute << std::setw(2) << time.wSecond << '-'
      << std::setw(3) << time.wMilliseconds << '-' << GetCurrentProcessId();
  return out.str();
}
void check_drive(const wchar_t* root, uint64_t bytes) {
  UINT kind = GetDriveTypeW(root);
  if (kind != DRIVE_FIXED && kind != DRIVE_REMOVABLE)
    throw std::runtime_error(fs::path(root).string() + " is not an available local disk");
  auto available = fs::space(fs::path(root)).available;
  if (available < bytes + 16 * 1024 * 1024)
    throw std::runtime_error(fs::path(root).string() + " does not have enough free space for the new test file");
}
DWORD run_child(const fs::path& executable, const std::vector<std::wstring>& args, const fs::path& log) {
  std::wstring command = quote(executable.wstring());
  for (const auto& arg : args) { command += L' '; command += quote(arg); }
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle output(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (output.value == INVALID_HANDLE_VALUE) throw win_error("create process log");
  // Open NUL explicitly so redirected standard input is always a valid handle.
  Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (input.value == INVALID_HANDLE_VALUE) throw win_error("open child input");
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = input.value; startup.hStdOutput = output.value; startup.hStdError = output.value;
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                      nullptr, nullptr, &startup, &process)) throw win_error("start executable");
  Handle process_handle(process.hProcess), thread_handle(process.hThread);
  if (WaitForSingleObject(process_handle.value, INFINITE) != WAIT_OBJECT_0) throw win_error("wait for executable");
  DWORD code = 0;
  if (!GetExitCodeProcess(process_handle.value, &code)) throw win_error("read executable exit code");
  return code;
}
void require_child(const fs::path& exe, const std::vector<std::wstring>& args, const fs::path& log) {
  DWORD code = run_child(exe, args, log);
  if (code != 0) throw std::runtime_error(exe.filename().string() + " failed (exit " +
      std::to_string(code) + "). See " + log.string());
}
std::string read_text(const fs::path& file) {
  std::ifstream input(file); std::ostringstream text; text << input.rdbuf(); return text.str();
}
int execute(const Config& c) {
  const auto bin = executable_dir();
  for (const char* name : {"blr.exe", "gen_data.exe", "gen_trace.exe"})
    if (!fs::is_regular_file(bin / name)) throw std::runtime_error("missing companion executable: " + (bin / name).string());
  const uint64_t bytes = c.size_mib * 1024 * 1024;
  check_drive(L"D:\\", bytes); check_drive(L"E:\\", bytes);
  const std::string id = run_id();
  auto output_root = c.output.empty() ? default_output() : c.output;
  const auto logs = output_root / id;
  const auto dir_a = fs::path(L"D:\\BudgetLimitedReads") / id;
  const auto dir_b = fs::path(L"E:\\BudgetLimitedReads") / id;
  fs::create_directories(output_root);
  if (!fs::create_directory(logs)) throw std::runtime_error("run output directory already exists");
  std::cout << "Real HDD run " << id << "\nDrive A: " << (dir_a / "mirror.bin").string()
            << "\nDrive B: " << (dir_b / "mirror.bin").string() << "\nResults: " << logs.string()
            << "\nCreating " << c.size_mib << " MiB of test data on EACH HDD. Existing files are left in place.\n" << std::flush;
  std::ofstream summary(logs / "run-summary.txt");
  summary << "Windows 11 real D/E HDD run: " << id << "\n"
          << "file_a=" << (dir_a / "mirror.bin").string() << "\nfile_b=" << (dir_b / "mirror.bin").string()
          << "\nfile_bytes=" << bytes << "\nrate=" << c.rate << "\nduration_seconds=" << c.duration
          << "\ndelay_us=" << c.delay_us << "\npermits=" << c.permits << "\nthreshold=" << c.threshold
          << "\nmax_outstanding=" << c.cap << "\ntrace_seed=42\nreal_disk_reads=true\n"
          << "Completion times are observed polling times. This is a functional comparison, not a calibrated benchmark.\n";
  summary.flush();
  if (!summary) throw std::runtime_error("cannot write run summary");
  try {
    fs::create_directories(dir_a.parent_path()); fs::create_directories(dir_b.parent_path());
    if (!fs::create_directory(dir_a) || !fs::create_directory(dir_b)) throw std::runtime_error("test run directory already exists");
    require_child(bin / "gen_data.exe", {std::to_wstring(bytes), (dir_a / "mirror.bin").wstring()}, logs / "create-D.txt");
    require_child(bin / "gen_data.exe", {std::to_wstring(bytes), (dir_b / "mirror.bin").wstring()}, logs / "create-E.txt");
    const auto trace = logs / "trace.csv";
    require_child(bin / "gen_trace.exe", {std::to_wstring(bytes), std::to_wstring(c.rate),
      std::to_wstring(c.duration), L"42", trace.wstring()}, logs / "create-trace.txt");
    unsigned failed = 0;
    for (const wchar_t* mode : {L"rr", L"lessbusy", L"fixed", L"budget"}) {
      std::wstring name(mode);
      std::cout << "Running " << fs::path(name).string() << " on D: and E:...\n" << std::flush;
      DWORD code = run_child(bin / "blr.exe", {L"--mode", name, L"--trace", trace.wstring(),
        L"--file-a", (dir_a / "mirror.bin").wstring(), L"--file-b", (dir_b / "mirror.bin").wstring(),
        L"--delay-us", std::to_wstring(c.delay_us), L"--permits", std::to_wstring(c.permits),
        L"--threshold", std::to_wstring(c.threshold), L"--max-outstanding", std::to_wstring(c.cap),
        L"--out-prefix", (logs / name).wstring()}, logs / (name + L".console.txt"));
      summary << "\nmode=" << fs::path(name).string() << " exit=" << code << '\n'
              << read_text(logs / (name + L".console.txt"));
      summary.flush();
      if (!summary) throw std::runtime_error("cannot update run summary");
      if (code) { ++failed; std::cout << "Mode failed; see its console log.\n"; }
    }
    summary << "\nrun_completed=true\nfailed_modes=" << failed << '\n'; summary.close();
    if (!summary) throw std::runtime_error("cannot finish run summary");
    std::cout << "\nFinished. Failed modes: " << failed << "\nResults: " << logs.string()
              << "\nTest files remain in the printed D: and E: run directories.\n";
    return failed ? 2 : 0;
  } catch (const std::exception& error) {
    summary << "\nrun_completed=false\nerror=" << error.what() << '\n'; summary.flush(); throw;
  }
}
}
int main(int argc, char** argv) {
  bool pause = argc == 1;
  int code = 1;
  try {
    auto config = parse(argc, argv); pause = config.pause;
    if (config.help) {
      std::cout << "blr_hdd.exe: real mirrored reads on separate D: and E: HDDs\n"
        << "No arguments: new 64 MiB file per drive, 100 reads/s, 10 seconds per mode, 5 ms hedge delay.\n"
        << "Options: --size-mib N --rate N --duration N --delay-us N --permits N\n"
        << "         --threshold N --max-outstanding N --output-dir PATH --no-pause\n";
      return 0;
    }
    code = execute(config);
  } catch (const std::exception& error) { std::cerr << "ERROR: " << error.what() << '\n'; }
  if (pause) { std::cout << "\nPress Enter to close..." << std::flush; std::cin.get(); }
  return code;
}
