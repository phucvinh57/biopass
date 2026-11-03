#include "sealed_store.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sealed_file.h"

namespace biopass {

namespace {

// Closes the fd on scope exit.
class Fd {
 public:
  explicit Fd(int fd = -1) : fd_(fd) {}
  ~Fd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int get() const { return fd_; }

 private:
  int fd_;
};

bool safeDir(int fd, uid_t owner) {
  struct stat st{};
  return fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == owner &&
         (st.st_mode & (S_IWGRP | S_IWOTH)) == 0;
}

// Opens `name` under `parent` (AT_FDCWD for an absolute path) as a directory,
// creating it with `mode` if asked. -1 with errno = ENOENT when it is missing,
// EPERM when it fails the ownership/mode check.
int openDir(int parent, const std::string& name, bool create, mode_t mode, uid_t owner) {
  constexpr int kFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
  int fd = openat(parent, name.c_str(), kFlags);
  if (fd < 0 && errno == ENOENT && create) {
    if (mkdirat(parent, name.c_str(), mode) != 0 && errno != EEXIST) {
      return -1;
    }
    fd = openat(parent, name.c_str(), kFlags);
    // mkdir applies the umask; set the intended mode on what we created.
    if (fd >= 0 && safeDir(fd, owner)) {
      fchmod(fd, mode);
    }
  }
  if (fd < 0) {
    return -1;
  }
  if (!safeDir(fd, owner)) {
    close(fd);
    errno = EPERM;
    return -1;
  }
  return fd;
}

// Opens the store directory (and its parent). Returns the store dir fd, or -1
// with `result` set.
int openStore(const SealedStore& store, bool create, StoreResult& result) {
  const size_t slash = store.dir.find_last_of('/');
  if (slash == std::string::npos || slash == 0 || slash + 1 == store.dir.size()) {
    result = StoreResult::Error;
    return -1;
  }
  Fd parent(openDir(AT_FDCWD, store.dir.substr(0, slash), create, 0755, store.owner));
  if (parent.get() < 0) {
    result = errno == ENOENT  ? StoreResult::NotFound
             : errno == EPERM ? StoreResult::Unsafe
                              : StoreResult::Error;
    return -1;
  }
  const int dir = openDir(parent.get(), store.dir.substr(slash + 1), create, 0711, store.owner);
  if (dir < 0) {
    result = errno == ENOENT  ? StoreResult::NotFound
             : errno == EPERM ? StoreResult::Unsafe
                              : StoreResult::Error;
  }
  return dir;
}

bool writeAll(int fd, const uint8_t* data, size_t len) {
  while (len > 0) {
    const ssize_t n = write(fd, data, len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    data += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}

// Atomically replaces `name` in the store with `data` (temp file, fsync,
// rename, fsync dir), creating the directories as needed.
StoreResult writeAtomic(const SealedStore& store, const std::string& name,
                        const std::vector<uint8_t>& data, mode_t mode) {
  StoreResult result = StoreResult::Error;
  Fd dir(openStore(store, /*create=*/true, result));
  if (dir.get() < 0) {
    return result;
  }

  const std::string tmp = "." + name + ".tmp";
  constexpr int kTmpFlags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC;
  int raw = openat(dir.get(), tmp.c_str(), kTmpFlags, 0600);
  if (raw < 0 && errno == EEXIST) {
    // Left over from an interrupted save.
    unlinkat(dir.get(), tmp.c_str(), 0);
    raw = openat(dir.get(), tmp.c_str(), kTmpFlags, 0600);
  }
  if (raw < 0) {
    return StoreResult::Error;
  }
  {
    Fd file(raw);
    if (fchmod(file.get(), mode) != 0 || !writeAll(file.get(), data.data(), data.size()) ||
        fsync(file.get()) != 0) {
      unlinkat(dir.get(), tmp.c_str(), 0);
      return StoreResult::Error;
    }
  }
  if (renameat(dir.get(), tmp.c_str(), dir.get(), name.c_str()) != 0) {
    unlinkat(dir.get(), tmp.c_str(), 0);
    return StoreResult::Error;
  }
  fsync(dir.get());
  return StoreResult::Ok;
}

StoreResult removeAt(const SealedStore& store, const std::string& name) {
  StoreResult result = StoreResult::Error;
  Fd dir(openStore(store, /*create=*/false, result));
  if (dir.get() < 0) {
    return result;
  }
  if (unlinkat(dir.get(), name.c_str(), 0) != 0) {
    return errno == ENOENT ? StoreResult::NotFound : StoreResult::Error;
  }
  fsync(dir.get());
  return StoreResult::Ok;
}

}  // namespace

std::string sealedFileName(uid_t uid) { return std::to_string(uid) + ".sealed"; }

std::string sealedModeFileName(uid_t uid) { return std::to_string(uid) + ".mode"; }

StoreResult writeSealed(const SealedStore& store, uid_t uid, const std::vector<uint8_t>& data) {
  if (data.empty() || data.size() > kMaxSealedFileSize) {
    return StoreResult::Error;
  }
  return writeAtomic(store, sealedFileName(uid), data, 0600);
}

StoreResult writeSealedMode(const SealedStore& store, uid_t uid, bool pcr7) {
  const std::string text = pcr7 ? "pcr7\n" : "none\n";
  return writeAtomic(store, sealedModeFileName(uid), std::vector<uint8_t>(text.begin(), text.end()),
                     0644);
}

StoreResult readSealed(const SealedStore& store, uid_t uid, std::vector<uint8_t>& out) {
  out.clear();
  StoreResult result = StoreResult::Error;
  Fd dir(openStore(store, /*create=*/false, result));
  if (dir.get() < 0) {
    return result;
  }
  const std::string name = sealedFileName(uid);
  Fd file(openat(dir.get(), name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
  if (file.get() < 0) {
    return errno == ENOENT  ? StoreResult::NotFound
           : errno == ELOOP ? StoreResult::Unsafe
                            : StoreResult::Error;
  }
  struct stat st{};
  if (fstat(file.get(), &st) != 0) {
    return StoreResult::Error;
  }
  if (!S_ISREG(st.st_mode) || st.st_uid != store.owner || (st.st_mode & 077) != 0) {
    return StoreResult::Unsafe;
  }
  if (st.st_size <= 0 || static_cast<size_t>(st.st_size) > kMaxSealedFileSize) {
    return StoreResult::Error;
  }
  out.resize(kMaxSealedFileSize + 1);
  size_t got = 0;
  while (got < out.size()) {
    const ssize_t n = read(file.get(), out.data() + got, out.size() - got);
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0) {
      out.clear();
      return StoreResult::Error;
    }
    if (n == 0) {
      break;
    }
    got += static_cast<size_t>(n);
  }
  if (got == 0 || got > kMaxSealedFileSize) {
    out.clear();
    return StoreResult::Error;
  }
  out.resize(got);
  return StoreResult::Ok;
}

StoreResult removeSealed(const SealedStore& store, uid_t uid) {
  return removeAt(store, sealedFileName(uid));
}

StoreResult removeSealedMode(const SealedStore& store, uid_t uid) {
  return removeAt(store, sealedModeFileName(uid));
}

}  // namespace biopass
