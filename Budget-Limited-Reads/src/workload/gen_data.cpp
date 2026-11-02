#include <stdexcept>
#include <string>
// Creates one new immutable-pattern file; never overwrites an existing path.
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif
#include <cerrno>
#include <iostream>
#include <vector>
#include "mirror/verify.h"
int main(int argc, char** argv) {
  int fd=-1;
  try {
    if(argc!=3) throw std::runtime_error("usage: gen_data FILE_BYTES NEW_FILE_PATH");
    std::string size=argv[1];
    if(size.empty() || size.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("invalid size");
    uint64_t bytes=std::stoull(size);
    if(!bytes || bytes%4096 || bytes>INT64_MAX) throw std::runtime_error("size must be a positive multiple of 4096");
#ifdef _WIN32
    _sopen_s(&fd, argv[2], _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
             _SH_DENYWR, _S_IREAD | _S_IWRITE);
#else
    fd=open(argv[2],O_WRONLY|O_CREAT|O_EXCL,0600);
#endif
    if(fd<0) throw std::runtime_error("cannot create new file (existing paths are refused)");
    std::vector<uint8_t> data(4096);
    for(uint64_t offset=0;offset<bytes;offset+=data.size()) {
      blr::fill_expected(offset,data.size(),data.data());
      size_t written=0;
      while(written<data.size()) {
#ifdef _WIN32
        auto n=_write(fd,data.data()+written,static_cast<unsigned>(data.size()-written));
#else
        auto n=write(fd,data.data()+written,data.size()-written);
#endif
        if(n<0 && errno==EINTR) continue;
        if(n<=0) throw std::runtime_error("write failed; incomplete file remains");
        written+=n;
      }
    }
#ifdef _WIN32
    if(_commit(fd)<0)
#else
    if(fsync(fd)<0)
#endif
      throw std::runtime_error("fsync failed");
#ifdef _WIN32
    if(_close(fd)<0)
#else
    if(close(fd)<0)
#endif
    { fd=-1; throw std::runtime_error("close failed"); }
    fd=-1;
  } catch(const std::exception& e) {
    if(fd>=0) {
#ifdef _WIN32
      _close(fd);
#else
      close(fd);
#endif
    }
    std::cerr<<e.what()<<'\n'; return 1;
  }
}
