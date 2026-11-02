#include "engine/windows_backend.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cerrno>
#include <filesystem>
#include <limits>
#include <map>
#include <stdexcept>
#include <system_error>

namespace blr {
namespace {
std::runtime_error windows_error(const char* action, DWORD code) {
  return std::runtime_error(std::string(action) + ": " +
      std::system_category().message(static_cast<int>(code)) + " (Windows error " + std::to_string(code) + ")");
}
// Keep shared CSV results in the bytes-or-negative-errno convention.
int64_t read_error(DWORD code) {
  switch (code) {
    case ERROR_HANDLE_EOF: return 0;
    case ERROR_ACCESS_DENIED: return -EACCES;
    case ERROR_INVALID_PARAMETER: return -EINVAL;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
    case ERROR_NO_SYSTEM_RESOURCES: return -ENOMEM;
    default: return -EIO;
  }
}
}
struct WindowsBackend::Impl {
  struct Operation {
    OVERLAPPED overlapped{};
    Drive drive = Drive::A;
    bool immediate_error = false;
    int64_t result = 0;
    ~Operation() { if (overlapped.hEvent) CloseHandle(overlapped.hEvent); }
  };
  HANDLE files[2] = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
  size_t counts[2] = {0, 0};
  size_t cap = 0;
  // Stable heap storage is mandatory: Windows retains each OVERLAPPED pointer.
  std::map<ReplicaId, std::unique_ptr<Operation>> pending;
  ~Impl() {
    // Defensive wait, without cancellation. The reader normally drains first.
    for (auto& item : pending) {
      auto& op = *item.second;
      if (!op.immediate_error) {
        DWORD bytes = 0;
        GetOverlappedResult(files[static_cast<int>(op.drive)], &op.overlapped, &bytes, TRUE);
      }
    }
    pending.clear();
    for (HANDLE file : files) if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  }
};
WindowsBackend::WindowsBackend(const std::string& a, const std::string& b, unsigned depth)
    : impl_(new Impl) {
  if (!depth) throw std::invalid_argument("queue depth must be positive");
  impl_->cap = depth;
  const std::string paths[2] = {a, b};
  for (int i = 0; i < 2; ++i) {
    auto path = std::filesystem::path(paths[i]);
    impl_->files[i] = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, nullptr);
    if (impl_->files[i] == INVALID_HANDLE_VALUE)
      throw windows_error("open mirrored file", GetLastError());
  }
}
WindowsBackend::~WindowsBackend() = default;
bool WindowsBackend::submit(const ReadSubmission& req) {
  if (impl_->pending.size() >= impl_->cap) return false;
  if (!req.buffer || !req.length || req.length > (std::numeric_limits<DWORD>::max)())
    throw std::invalid_argument("invalid Windows read buffer or length");
  auto operation = std::make_unique<Impl::Operation>();
  operation->drive = req.drive;
  operation->overlapped.Offset = static_cast<DWORD>(req.offset);
  operation->overlapped.OffsetHigh = static_cast<DWORD>(req.offset >> 32);
  operation->overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!operation->overlapped.hEvent) throw windows_error("create read event", GetLastError());
  auto inserted = impl_->pending.emplace(req.id, std::move(operation));
  if (!inserted.second) throw std::invalid_argument("duplicate replica ID");
  auto& op = *inserted.first->second;
  ++impl_->counts[static_cast<int>(req.drive)];
  BOOL ok = ReadFile(impl_->files[static_cast<int>(req.drive)], req.buffer,
      static_cast<DWORD>(req.length), nullptr, &op.overlapped);
  if (!ok) {
    DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      // Report permanent failures as completions rather than backpressure, so
      // malformed/EOF reads cannot trap the caller in a submission-retry loop.
      op.immediate_error = true;
      op.result = read_error(error);
    }
  }
  return true;
}
size_t WindowsBackend::poll(std::vector<ReadCompletion>& out) {
  size_t added = 0;
  for (auto it = impl_->pending.begin(); it != impl_->pending.end();) {
    auto& op = *it->second;
    int64_t result = op.result;
    if (!op.immediate_error) {
      DWORD bytes = 0;
      BOOL ok = GetOverlappedResult(impl_->files[static_cast<int>(op.drive)],
          &op.overlapped, &bytes, FALSE);
      if (!ok) {
        DWORD error = GetLastError();
        if (error == ERROR_IO_INCOMPLETE) { ++it; continue; }
        result = read_error(error);
      } else result = bytes;
    }
    out.push_back({it->first, op.drive, result});
    --impl_->counts[static_cast<int>(op.drive)];
    it = impl_->pending.erase(it);
    ++added;
  }
  return added;
}
size_t WindowsBackend::outstanding(Drive d) const {
  return impl_->counts[static_cast<int>(d)];
}
}
