#include "buffer.h"

const char Buffer::kCRLF[] = "\r\n";
const char Buffer::CONTENT[] = "Content-Length";
const int32_t Buffer::kCheapPrepend;
const int32_t Buffer::kInitialSize;

ssize_t Buffer::readFd(int32_t fd, int32_t *saveErrno) {
  char extrabuf[65536];
  IOV_TYPE vec[2];
  const int32_t writable = writableBytes();
#ifdef _WIN64
  vec[0].buf = begin() + writerIndex;
  vec[0].len = writable;
  vec[1].buf = extrabuf;
  vec[1].len = sizeof(extrabuf);
#else
  vec[0].iov_base = begin() + writerIndex;
  vec[0].iov_len = writable;
  vec[1].iov_base = extrabuf;
  vec[1].iov_len = sizeof extrabuf;
#endif
  const int iovcnt = (writable < sizeof(extrabuf)) ? 2 : 1;
  const ssize_t n = Socket::readv(fd, vec, iovcnt);
  if (n < 0) {
#ifdef _WIN64
    *saveErrno = GetLastError();
#else
    *saveErrno = errno;
#endif
  } else if (n <= writable) {
    writerIndex += n;
  } else {
    writerIndex = buffer.size();
    append(extrabuf, n - writable);
  }
  return n;
}
