// Checks that files the user controls, face images included, are read only
// when they are regular files (core/user_file.h): no device, FIFO or
// oversized file is read, and nothing blocks. Runs as any user; the extra
// checks for the root helper run when started with sudo or inside
// `unshare --map-root-user --map-auto`.

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "image_utils.h"
#include "user_file.h"

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
  std::printf("[%s] %s\n", cond ? "PASS" : "FAIL", what);
  if (!cond) {
    g_failures++;
  }
}

std::string makeTempDir() {
  std::string tmpl = "/tmp/biopass-userfile-XXXXXX";
  char* dir = mkdtemp(tmpl.data());
  return dir ? std::string(dir) : std::string();
}

void writeFile(const std::string& path, const std::string& content) {
  std::ofstream(path) << content;
}

void checkReadRegularFile(const std::string& dir) {
  const std::string file = dir + "/file";
  writeFile(file, "hello");
  const auto data = biopass::readRegularFile(file, 1024);
  expect(data && *data == "hello", "regular file is read");

  expect(!biopass::readRegularFile(file, 4), "file over the size cap is refused");
  expect(biopass::readRegularFile(file, 5).has_value(), "file at the size cap is read");
  expect(!biopass::readRegularFile(dir + "/missing", 1024), "missing file is refused");
  expect(!biopass::readRegularFile(dir, 1024), "directory is refused");

  const std::string link = dir + "/link";
  expect(symlink(file.c_str(), link.c_str()) == 0, "plant symlink to the file");
  const auto via_link = biopass::readRegularFile(link, 1024);
  expect(via_link && *via_link == "hello", "symlink to a regular file is followed");

  const std::string zero = dir + "/zero";
  expect(symlink("/dev/zero", zero.c_str()) == 0, "plant symlink to /dev/zero");
  expect(!biopass::readRegularFile(zero, 1024), "symlink to /dev/zero is refused");
  expect(!biopass::readRegularFile("/dev/null", 1024), "character device is refused");

  // A FIFO with no writer would block a plain open() forever; alarm() turns
  // a hang into a failed run.
  const std::string fifo = dir + "/fifo";
  expect(mkfifo(fifo.c_str(), 0600) == 0, "create FIFO");
  alarm(10);
  expect(!biopass::readRegularFile(fifo, 1024), "FIFO is refused without blocking");
  alarm(0);
}

// Enrolled face images go through the same reader (face/image_utils.h).
void checkImages(const std::string& dir) {
  const uint8_t pixels[2 * 2 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 10, 20, 30};
  const std::string png = dir + "/face.png";
  expect(stbi_write_png(png.c_str(), 2, 2, 3, pixels, 2 * 3) != 0, "write test PNG");
  const ImageRGB img = readImage(png);
  expect(img.width == 2 && img.height == 2 && img.at(1, 1, 2) == 30, "regular image is decoded");

  const std::string zero = dir + "/zero.png";
  expect(symlink("/dev/zero", zero.c_str()) == 0, "plant image symlink to /dev/zero");
  expect(readImage(zero).empty(), "image symlinked to /dev/zero is refused");
  expect(decodeImage("not an image").empty(), "garbage bytes decode to an empty image");
}

// Root reading through a path the user controls must get only what the user
// could read.
void checkReadUserFileAsRoot(const std::string& dir) {
  const std::string user = "nobody";
  chmod(dir.c_str(), 0755);

  const std::string shared = dir + "/shared";
  writeFile(shared, "public");
  chmod(shared.c_str(), 0644);
  const auto data = biopass::readUserFile(user, shared, 1024);
  expect(data && *data == "public", "root reads a file the user can read");

  const std::string secret = dir + "/secret";
  writeFile(secret, "secret");
  chmod(secret.c_str(), 0600);
  expect(!biopass::readUserFile(user, secret, 1024), "root can't read what the user can't");
  expect(biopass::readRegularFile(secret, 1024).has_value(), "root still reads it without guard");

  expect(!biopass::readUserFile("biopass-no-such-user", shared, 1024), "unknown user is refused");
}

}  // namespace

int main() {
  const std::string dir = makeTempDir();
  expect(!dir.empty(), "create temp dir");

  checkReadRegularFile(dir);
  checkImages(dir);
  if (geteuid() == 0) {
    checkReadUserFileAsRoot(dir);
  } else {
    std::printf("SKIP root checks (run with sudo or unshare --map-root-user --map-auto)\n");
  }

  std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES");
  return g_failures == 0 ? 0 : 1;
}
