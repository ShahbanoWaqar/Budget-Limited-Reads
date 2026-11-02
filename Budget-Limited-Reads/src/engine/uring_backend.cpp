#include "engine/uring_backend.h"
#include <liburing.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
namespace blr {
struct UringBackend::Impl {
  io_uring ring{};
  bool initialized = false;
  int fd[2] = {-1, -1};
  unsigned cap = 0;
  size_t outstanding[2] = {0, 0};
  std::unordered_map<ReplicaId, Drive> pending;
  ~Impl() {
    // Reader owns draining and must outlive all accepted reads.
    if (initialized) io_uring_queue_exit(&ring);
    for (int f : fd) if (f >= 0) close(f);
  }
};
UringBackend::UringBackend(const std::string& a, const std::string& b, unsigned depth)
    : impl_(new Impl) {
  if (!depth) throw std::invalid_argument("queue depth must be positive");
  int rc = io_uring_queue_init(depth, &impl_->ring, 0);
  if (rc < 0) throw std::runtime_error(std::string("io_uring: ") + strerror(-rc));
  impl_->initialized = true; impl_->cap = depth;
  impl_->fd[0] = open(a.c_str(), O_RDONLY | O_DIRECT);
  impl_->fd[1] = open(b.c_str(), O_RDONLY | O_DIRECT);
  if (impl_->fd[0] < 0 || impl_->fd[1] < 0)
    throw std::runtime_error(std::string("open mirrored files: ") + strerror(errno));
}
UringBackend::~UringBackend() = default;
bool UringBackend::submit(const ReadSubmission& r) {
  if (impl_->pending.size() >= impl_->cap) return false;
  auto* sqe = io_uring_get_sqe(&impl_->ring);
  if (!sqe) return false;
  impl_->pending.emplace(r.id, r.drive);
  io_uring_prep_read(sqe, impl_->fd[static_cast<int>(r.drive)], r.buffer, r.length, r.offset);
  io_uring_sqe_set_data64(sqe, r.id);
  ++impl_->outstanding[static_cast<int>(r.drive)];
  // SQEs are now owned by the ring. Keep ownership even on transient submission
  // failure; poll retries the pending SQ instead of freeing an exposed buffer.
  io_uring_submit(&impl_->ring);
  return true;
}
size_t UringBackend::poll(std::vector<ReadCompletion>& out) {
  int rc = io_uring_submit(&impl_->ring);
  if (rc < 0 && rc != -EINTR && rc != -EAGAIN && rc != -EBUSY)
    throw std::runtime_error(std::string("io_uring_submit: ") + strerror(-rc));
  size_t n = 0;
  io_uring_cqe* cqe;
  while (io_uring_peek_cqe(&impl_->ring, &cqe) == 0) {
    auto id = io_uring_cqe_get_data64(cqe);
    auto it = impl_->pending.find(id);
    if (it == impl_->pending.end()) throw std::runtime_error("unknown replica completion");
    out.push_back({id, it->second, cqe->res});
    --impl_->outstanding[static_cast<int>(it->second)];
    impl_->pending.erase(it); io_uring_cqe_seen(&impl_->ring, cqe); ++n;
  }
  return n;
}
size_t UringBackend::outstanding(Drive d) const { return impl_->outstanding[static_cast<int>(d)]; }
}
