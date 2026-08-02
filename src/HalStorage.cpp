#include "HalStorage.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

// Defined below with the IO accounting; declared here because the open site
// comes first in this file.
void halStorageMarkOpen();
void halStorageNoteRead(size_t bytes);

HalStorage HalStorage::instance;
HalStorage::HalStorage() {}

namespace {
std::string configuredStorageRoot() {
  const char *root = std::getenv("CROSSPOINT_SIM_SD");
  if (!root || !*root) {
    root = std::getenv("CROSSPOINT_EMU_SD");
  }
  return (root && *root) ? std::string(root) : std::string("./fs_");
}

bool containsUnsafeSegment(const std::string &path) {
  std::stringstream stream(path);
  std::string segment;
  while (std::getline(stream, segment, '/')) {
    if (segment == "..") {
      return true;
    }
  }
  return false;
}

std::string resolveStoragePath(const char *path) {
  std::string logical = path ? std::string(path) : std::string("/");
  if (logical.empty()) {
    logical = "/";
  }
  if (containsUnsafeSegment(logical)) {
    fprintf(stderr, "[SIM] rejected unsafe storage path: %s\n",
            logical.c_str());
    return {};
  }
  while (!logical.empty() && logical.front() == '/') {
    logical.erase(logical.begin());
  }

  std::string root = configuredStorageRoot();
  while (root.size() > 1 && root.back() == '/') {
    root.pop_back();
  }
  if (logical.empty()) {
    return root;
  }
  return root + "/" + logical;
}

bool ensureParentDirectories(const std::string &full) {
  const size_t slash = full.find_last_of('/');
  if (slash == std::string::npos) {
    return true;
  }
  const std::string parent = full.substr(0, slash);
  if (parent.empty()) {
    return true;
  }
  for (size_t i = 1; i < parent.size(); ++i) {
    if (parent[i] == '/') {
      ::mkdir(parent.substr(0, i).c_str(), 0777);
    }
  }
  return ::mkdir(parent.c_str(), 0777) == 0 || errno == EEXIST;
}
} // namespace

bool HalStorage::begin() {
  const std::string root = configuredStorageRoot();
  for (size_t i = 1; i < root.size(); ++i) {
    if (root[i] == '/') {
      ::mkdir(root.substr(0, i).c_str(), 0777);
    }
  }
  return ::mkdir(root.c_str(), 0777) == 0 || errno == EEXIST;
}
bool HalStorage::ready() const { return true; }

class HalFile::Impl {
public:
  int fd = -1;
  std::string path;
  DIR *dir = nullptr;

  // READ-AHEAD BUFFER, mirroring lib/hal/HalStorage.cpp on the device.
  //
  // Duplicated deliberately, not shared: the simulator reimplements HalStorage
  // against POSIX and the device implements it against SdFat, so there is no
  // common file to put this in. Buffering here buys no speed — the host page
  // cache is already faster than any card — but it is the ONLY way the logic is
  // ever exercised. Every simulator gate reads through this, so a bug in the
  // position/seek/write interaction shows up in the EPUB corpus rather than on
  // a device nobody has yet.
  //
  // Invariant: when bytes are buffered, the fd is AHEAD of the caller's logical
  // position by exactly (bufLen - bufPos).
  static constexpr size_t kBufBytes = 512;
  uint8_t buf[kBufBytes] = {};
  size_t bufLen = 0;
  size_t bufPos = 0;

  size_t buffered() const { return bufLen - bufPos; }

  void dropBuffer() {
    bufLen = 0;
    bufPos = 0;
  }

  // Put the fd back where the caller thinks it is, then drop the buffer.
  void syncDown() {
    const size_t ahead = buffered();
    dropBuffer();
    if (ahead > 0 && fd >= 0)
      lseek(fd, -static_cast<off_t>(ahead), SEEK_CUR);
  }

  size_t refill() {
    dropBuffer();
    if (fd < 0)
      return 0;
    const ssize_t n = ::read(fd, buf, kBufBytes);
    if (n > 0) {
      bufLen = static_cast<size_t>(n);
      halStorageNoteRead(bufLen);
    }
    return bufLen;
  }

  bool open(const char *p, int flags) {
    path = p;
    // The simulator's FsApiConstants.h just includes <fcntl.h> and typedef int
    // oflag_t, so all O_* constants are already native POSIX values — pass them
    // straight through.
    fd = ::open(path.c_str(), flags, 0666);
    if (fd >= 0) halStorageMarkOpen();
    if (fd < 0) {
      fprintf(stderr, "[SIM] open failed: %s (flags=0x%x errno=%d %s)\n",
              path.c_str(), flags, errno, strerror(errno));
    }
    return fd >= 0;
  }

  bool openAsDir(const char *p) {
    path = p;
    dir = opendir(p);
    return dir != nullptr;
  }

  bool isDir() const { return dir != nullptr; }
  bool isOpen() const { return fd >= 0 || dir != nullptr; }

  // Release BOTH kinds of handle. See the destructor for why this exists.
  void release() {
    if (dir) {
      closedir(dir);
      dir = nullptr;
    }
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }
};

HalFile::HalFile() : impl(new Impl()) {}

// Releases BOTH kinds of handle, matching HalFile::close().
//
// It used to close only impl->fd and leave impl->dir open, so every HalFile
// holding a directory leaked its DIR* unless the caller happened to call close()
// by hand. LeakSanitizer measured 32,816 bytes per opendir, twice per run of the
// file browser.
//
// That is exactly backwards from the contract the firmware is written against:
// DESTRUCTOR_CLOSES_FILE=1 means callers deliberately do NOT close local
// handles, because the destructor is supposed to. FileBrowserActivity follows
// that rule and leaked as a result.
HalFile::~HalFile() {
  if (impl)
    impl->release();
}
HalFile::HalFile(HalFile &&other) : impl(std::move(other.impl)) {}
HalFile &HalFile::operator=(HalFile &&other) {
  if (this != &other) {
    if (impl)
      impl->release();
    impl = std::move(other.impl);
  }
  return *this;
}

void HalFile::flush() {
  if (impl && impl->fd >= 0) {
    impl->syncDown();
    fsync(impl->fd);
  }
}
bool HalFile::sync() {
  if (!impl || impl->fd < 0)
    return false;
  return fsync(impl->fd) == 0;
}
size_t HalFile::getName(char *name, size_t len) {
  if (!impl || impl->path.empty())
    return 0;
  size_t slash = impl->path.rfind('/');
  std::string fname =
      (slash == std::string::npos) ? impl->path : impl->path.substr(slash + 1);
  size_t n = std::min(fname.size(), len - 1);
  memcpy(name, fname.c_str(), n);
  name[n] = '\0';
  return n;
}
size_t HalFile::size() {
  if (!impl || impl->fd < 0)
    return 0;
  off_t cur = lseek(impl->fd, 0, SEEK_CUR);
  off_t end = lseek(impl->fd, 0, SEEK_END);
  lseek(impl->fd, cur, SEEK_SET);
  return end < 0 ? 0 : (size_t)end;
}
size_t HalFile::fileSize() { return size(); }
uint64_t HalFile::fileSize64() { return size(); }
// SEEKS drop the buffer. seekCur is relative to the CALLER's position, which is
// behind the fd's whenever anything is buffered, so it is resolved against
// position() and reissued absolute rather than passed through.
bool HalFile::seek(size_t pos) { return seekSet(pos); }
bool HalFile::seek64(uint64_t pos) {
  if (!impl || impl->fd < 0)
    return false;
  if (pos > static_cast<uint64_t>(std::numeric_limits<off_t>::max()))
    return false;
  impl->dropBuffer();
  return lseek(impl->fd, static_cast<off_t>(pos), SEEK_SET) >= 0;
}
bool HalFile::seekCur(int64_t offset) {
  if (!impl || impl->fd < 0)
    return false;
  const off_t here = lseek(impl->fd, 0, SEEK_CUR) - static_cast<off_t>(impl->buffered());
  impl->dropBuffer();
  return lseek(impl->fd, here + static_cast<off_t>(offset), SEEK_SET) >= 0;
}
bool HalFile::seekSet(size_t offset) {
  if (!impl || impl->fd < 0)
    return false;
  impl->dropBuffer();
  return lseek(impl->fd, (off_t)offset, SEEK_SET) >= 0;
}
int HalFile::available() const {
  if (!impl || impl->fd < 0)
    return 0;
  off_t cur = lseek(impl->fd, 0, SEEK_CUR);
  off_t end = lseek(impl->fd, 0, SEEK_END);
  lseek(impl->fd, cur, SEEK_SET);
  // Plus whatever is sitting unread in the buffer: the web server and WebDAV
  // stream on while (file.available()), and would stop short without it.
  return (int)(end - cur) + static_cast<int>(impl->buffered());
}
size_t HalFile::position() const {
  if (!impl || impl->fd < 0)
    return 0;
  off_t pos = lseek(impl->fd, 0, SEEK_CUR);
  if (pos < 0)
    return 0;
  // Minus the read-ahead: DictZip does seekSet(position() + subLen), which
  // would skip past real data if this reported where the fd happens to be.
  const size_t ahead = impl->buffered();
  return static_cast<size_t>(pos) < ahead ? 0 : static_cast<size_t>(pos) - ahead;
}
// STORAGE ACCOUNTING, for sizing a decision this simulator cannot make itself.
//
// The question is whether holding EPUB sections in PSRAM would meaningfully cut
// page-turn latency on the X4 Pro. Wall-clock here says nothing — the host's
// page cache is orders of magnitude faster than an SD card over SPI. What DOES
// transfer is the COUNT: how many reads and how many bytes a page turn asks the
// card for. That number is the same on both.
//
// Off unless INKBACK_SIM_IO_STATS is set, printed at exit.
namespace {
struct IoStats {
  unsigned long opens = 0;
  unsigned long reads = 0;
  unsigned long long bytes = 0;
  bool enabled = std::getenv("INKBACK_SIM_IO_STATS") != nullptr;
  // Reported as a RUNNING TOTAL rather than once at exit. The destructor never
  // fired: the simulator's QUIT does not leave main by returning, so a
  // report-at-teardown printed nothing at all and looked exactly like a
  // subsystem that was never called.
  void note() {
    if (!enabled) return;
    if ((opens + reads) % 100 == 0) {
      fprintf(stderr, "[SIM-IO] opens=%lu reads=%lu bytes=%llu\n", opens, reads, bytes);
    }
  }
};
IoStats &ioStats() {
  static IoStats s;
  return s;
}
}  // namespace

void halStorageMarkOpen() {
  if (!ioStats().enabled) return;
  ioStats().opens++;
  ioStats().note();
}

void halStorageNoteRead(const size_t bytes) {
  if (!ioStats().enabled) return;
  ioStats().reads++;
  ioStats().bytes += static_cast<unsigned long long>(bytes);
  ioStats().note();
}

int HalFile::read(void *buf, size_t count) {
  if (!impl || impl->fd < 0)
    return -1;
  if (count == 0)
    return 0;
  auto *out = static_cast<uint8_t *>(buf);
  size_t done = 0;

  const size_t fromBuf =
      impl->buffered() < count ? impl->buffered() : count;
  if (fromBuf > 0) {
    memcpy(out, impl->buf + impl->bufPos, fromBuf);
    impl->bufPos += fromBuf;
    done += fromBuf;
  }
  if (done == count)
    return static_cast<int>(done);

  // A request at least as large as the buffer goes straight through: buffering
  // it would copy every byte twice to save nothing.
  const size_t remaining = count - done;
  if (remaining >= HalFile::Impl::kBufBytes) {
    const ssize_t n = ::read(impl->fd, out + done, remaining);
    if (n > 0) {
      halStorageNoteRead(static_cast<size_t>(n));
      done += static_cast<size_t>(n);
    }
    return done > 0 ? static_cast<int>(done) : static_cast<int>(n);
  }

  if (impl->refill() == 0)
    return done > 0 ? static_cast<int>(done) : 0;
  const size_t take =
      impl->buffered() < remaining ? impl->buffered() : remaining;
  memcpy(out + done, impl->buf + impl->bufPos, take);
  impl->bufPos += take;
  done += take;
  return static_cast<int>(done);
}
// The single-byte read is what readPod() multiplies into hundreds of calls, so
// it is the one that most wants to hit RAM.
int HalFile::read() {
  if (!impl || impl->fd < 0)
    return -1;
  if (impl->bufPos >= impl->bufLen && impl->refill() == 0)
    return -1;
  return impl->buf[impl->bufPos++];
}
// WRITES sync down first. The fd is ahead while anything is buffered, so
// writing without rewinding would put the bytes past where the caller believes
// the cursor is — silent corruption rather than an error.
size_t HalFile::write(const void *buf, size_t count) {
  if (!impl || impl->fd < 0)
    return 0;
  impl->syncDown();
  ssize_t n = ::write(impl->fd, buf, count);
  return n < 0 ? 0 : (size_t)n;
}
size_t HalFile::write(const uint8_t *buf, size_t count) {
  return write(static_cast<const void *>(buf), count);
}
size_t HalFile::write(uint8_t b) {
  if (!impl || impl->fd < 0)
    return 0;
  impl->syncDown();
  return (::write(impl->fd, &b, 1) == 1) ? 1 : 0;
}
bool HalFile::rename(const char *newPath) {
  if (!impl || impl->path.empty()) {
    return false;
  }
  const std::string resolved = resolveStoragePath(newPath);
  if (resolved.empty()) {
    return false;
  }
  close();
  ensureParentDirectories(resolved);
  return ::rename(impl->path.c_str(), resolved.c_str()) == 0;
}
bool HalFile::isDirectory() const { return impl && impl->isDir(); }
void HalFile::rewindDirectory() {
  if (impl && impl->dir)
    rewinddir(impl->dir);
}
bool HalFile::close() {
  if (!impl)
    return true;
  // Drop, not syncDown: nothing reads this handle again, and seeking a file we
  // are about to close is work for no one's benefit.
  impl->dropBuffer();
  if (impl->dir) {
    closedir(impl->dir);
    impl->dir = nullptr;
  }
  if (impl->fd >= 0) {
    ::close(impl->fd);
    impl->fd = -1;
  }
  return true;
}
HalFile HalFile::openNextFile() {
  if (!impl || !impl->dir)
    return HalFile();
  while (true) {
    struct dirent *entry = readdir(impl->dir);
    if (!entry)
      return HalFile();
    if (entry->d_name[0] == '.')
      continue; // skip . and ..

    std::string childFsPath = impl->path;
    if (childFsPath.back() != '/')
      childFsPath += '/';
    childFsPath += entry->d_name;

    HalFile child;
    struct stat st;
    if (stat(childFsPath.c_str(), &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode)) {
      child.impl->openAsDir(childFsPath.c_str());
    } else {
      child.impl->open(childFsPath.c_str(), O_RDONLY);
    }
    return child;
  }
}
bool HalFile::isOpen() const {
  if (!impl)
    return false;
  return impl->isOpen();
}
HalFile::operator bool() const { return isOpen(); }

HalFile HalStorage::open(const char *path, const oflag_t oflag) {
  std::string full = resolveStoragePath(path);
  HalFile f;
  if (full.empty()) {
    return f;
  }
  if ((oflag & O_CREAT) != 0) {
    ensureParentDirectories(full);
  }
  struct stat st;
  if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    f.impl->openAsDir(full.c_str());
  } else {
    f.impl->open(full.c_str(), oflag);
  }
  return f;
}
bool HalStorage::mkdir(const char *path, const bool /*pFlag*/) {
  std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return false;
  }
  // Create all intermediate directories (mkdir -p semantics).
  for (size_t i = 1; i < full.size(); ++i) {
    if (full[i] == '/') {
      ::mkdir(full.substr(0, i).c_str(),
              0777); // ignore errors (may already exist)
    }
  }
  return ::mkdir(full.c_str(), 0777) == 0 || errno == EEXIST;
}
bool HalStorage::exists(const char *path) {
  std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return false;
  }
  struct stat buffer;
  return (stat(full.c_str(), &buffer) == 0);
}
bool HalStorage::remove(const char *path) {
  std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return false;
  }
  return ::remove(full.c_str()) == 0;
}
bool HalStorage::rename(const char *oldPath, const char *newPath) {
  std::string o = resolveStoragePath(oldPath);
  std::string n = resolveStoragePath(newPath);
  if (o.empty() || n.empty()) {
    return false;
  }
  ensureParentDirectories(n);
  return ::rename(o.c_str(), n.c_str()) == 0;
}
static bool removeDirRecursive(const std::string &full) {
  DIR *d = opendir(full.c_str());
  if (!d)
    return ::remove(full.c_str()) == 0; // might be a plain file
  struct dirent *entry;
  while ((entry = readdir(d)) != nullptr) {
    if (entry->d_name[0] == '.')
      continue;
    std::string child = full + "/" + entry->d_name;
    struct stat st;
    if (stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
      removeDirRecursive(child);
    } else {
      ::remove(child.c_str());
    }
  }
  closedir(d);
  return ::rmdir(full.c_str()) == 0;
}

bool HalStorage::rmdir(const char *path) {
  std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return false;
  }
  return removeDirRecursive(full);
}
bool HalStorage::removeDir(const char *path) {
  std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return false;
  }
  return removeDirRecursive(full);
}

String HalStorage::readFile(const char *path) {
  HalFile f = open(path, O_RDONLY);
  if (!f)
    return String("");
  size_t s = f.size();
  std::string content(s, '\0');
  f.read((void *)content.data(), s);
  return String(content);
}
bool HalStorage::readFileToStream(const char *path, Print &out,
                                  size_t chunkSize) {
  HalFile f = open(path, O_RDONLY);
  if (!f)
    return false;
  std::vector<char> buf(chunkSize);
  int n;
  while ((n = f.read(buf.data(), chunkSize)) > 0) {
    out.write(reinterpret_cast<const uint8_t *>(buf.data()), n);
  }
  return true;
}
size_t HalStorage::readFileToBuffer(const char *path, char *buffer,
                                    size_t bufferSize, size_t maxBytes) {
  HalFile f = open(path, O_RDONLY);
  if (!f)
    return 0;
  size_t toRead = bufferSize - 1;
  if (maxBytes > 0 && maxBytes < toRead)
    toRead = maxBytes;
  int n = f.read(buffer, toRead);
  if (n < 0)
    n = 0;
  buffer[n] = '\0';
  return n;
}
bool HalStorage::writeFile(const char *path, const String &content) {
  HalFile f = open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (!f)
    return false;
  f.write(content.c_str(), content.length());
  return true;
}
bool HalStorage::ensureDirectoryExists(const char *path) { return mkdir(path); }

bool HalStorage::openFileForRead(const char *moduleName, const char *path,
                                 HalFile &file) {
  file = open(path, O_RDONLY);
  return file.isOpen();
}
bool HalStorage::openFileForRead(const char *moduleName,
                                 const std::string &path, HalFile &file) {
  return openFileForRead(moduleName, path.c_str(), file);
}
bool HalStorage::openFileForRead(const char *moduleName, const String &path,
                                 HalFile &file) {
  return openFileForRead(moduleName, path.c_str(), file);
}
bool HalStorage::openFileForWrite(const char *moduleName, const char *path,
                                  HalFile &file) {
  // O_RDWR, not O_WRONLY: the SDK function this stands in for opens
  // O_RDWR|O_CREAT|O_TRUNC (SDCardManager.cpp), and firmware relies on it.
  // Section::loadPageDuringBuild reads a finished page back out of the .bin it
  // is still appending to, through this same handle. With O_WRONLY that read
  // returns -1 and every long chapter renders blank.
  file = open(path, O_RDWR | O_CREAT | O_TRUNC);
  return file.isOpen();
}
bool HalStorage::openFileForWrite(const char *moduleName,
                                  const std::string &path, HalFile &file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}
bool HalStorage::openFileForWrite(const char *moduleName, const String &path,
                                  HalFile &file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

std::vector<String> HalStorage::listFiles(const char *path, int maxFiles) {
  std::vector<String> result;
  std::string full = resolveStoragePath(path);
  if (full.empty()) {
    return result;
  }
  DIR *dir = opendir(full.c_str());
  if (!dir)
    return result;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr && (int)result.size() < maxFiles) {
    if (entry->d_name[0] == '.')
      continue; // skip . and ..
    result.push_back(String(entry->d_name));
  }
  closedir(dir);
  return result;
}
