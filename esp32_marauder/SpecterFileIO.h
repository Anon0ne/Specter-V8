#pragma once
#include <stdio.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

namespace specter {
namespace fileio {
inline bool publishSnapshot(const char* path, const char* staging, const char* data, size_t size) {
  if (!path || !staging || !data) return false;
  struct stat info;
  if (::stat(path, &info) == 0 || errno != ENOENT) return false;
  const int fd = ::open(staging, O_CREAT | O_EXCL | O_WRONLY, 0644);
  if (fd < 0) return false;
  FILE* file = fdopen(fd, "w");
  if (!file) { ::close(fd); ::unlink(staging); return false; }
  bool okay = fwrite(data, 1, size, file) == size;
  if (fflush(file) != 0 || fsync(fileno(file)) != 0) okay = false;
  if (fclose(file) != 0) okay = false;
  if (okay) okay = ::rename(staging, path) == 0;
  if (!okay) ::unlink(staging);
  return okay;
}
inline bool appendSynced(const char* path, const char* data, size_t size) {
  if (!path || !data) return false;
  FILE* file = fopen(path, "a");
  if (!file) return false;
  bool okay = fwrite(data, 1, size, file) == size;
  if (fflush(file) != 0 || fsync(fileno(file)) != 0) okay = false;
  if (fclose(file) != 0) okay = false;
  return okay;
}
}
}
