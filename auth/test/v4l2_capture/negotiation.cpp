// Exercise the real V4L2 setup with a fake device that changes GREY to YUYV.
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "camera_capture.h"
#include "v4l2_capture.h"

extern "C" int __real_open(const char*, int, ...);
extern "C" int __real_close(int);
extern "C" int __real_fstat(int, struct stat*);
extern "C" int __real_ioctl(int, unsigned long, ...);

namespace {
constexpr const char* kDevice = "/dev/biopass-test-ir";
int g_path_fd = -1;
int g_capture_fd = -1;
uint32_t g_negotiated = V4L2_PIX_FMT_GREY;
int g_formats_set = 0;
int g_buffer_requests = 0;
int g_failures = 0;

void expect(bool ok, const char* what) {
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
  g_failures += !ok;
}
}  // namespace

extern "C" int __wrap_open(const char* path, int flags, ...) {
  if (std::strcmp(path, kDevice) == 0) {
    return g_path_fd = __real_open("/dev/null", O_RDONLY | O_CLOEXEC);
  }
  if (g_path_fd >= 0 && path == "/proc/self/fd/" + std::to_string(g_path_fd)) {
    return g_capture_fd = __real_open("/dev/null", O_RDONLY | O_CLOEXEC);
  }
  mode_t mode = 0;
  if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, mode_t);
    va_end(args);
  }
  return __real_open(path, flags, mode);
}

extern "C" int __wrap_close(int fd) {
  if (fd == g_path_fd)
    g_path_fd = -1;
  if (fd == g_capture_fd)
    g_capture_fd = -1;
  return __real_close(fd);
}

extern "C" int __wrap_fstat(int fd, struct stat* info) {
  const int result = __real_fstat(fd, info);
  if (result == 0 && (fd == g_path_fd || fd == g_capture_fd)) {
    info->st_mode = S_IFCHR | 0600;
    info->st_rdev = makedev(81, 200);
  }
  return result;
}

extern "C" int __wrap_ioctl(int fd, unsigned long request, void* arg) {
  if (fd != g_capture_fd)
    return __real_ioctl(fd, request, arg);
  switch (request) {
    case VIDIOC_QUERYCAP:
      static_cast<v4l2_capability*>(arg)->capabilities =
          V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;
      return 0;
    case VIDIOC_ENUM_FMT: {
      auto* format = static_cast<v4l2_fmtdesc*>(arg);
      if (format->index != 0)
        break;
      format->pixelformat = V4L2_PIX_FMT_GREY;
      return 0;
    }
    case VIDIOC_ENUM_FRAMESIZES: {
      auto* size = static_cast<v4l2_frmsizeenum*>(arg);
      if (size->index != 0)
        break;
      size->type = V4L2_FRMSIZE_TYPE_DISCRETE;
      size->discrete.width = size->discrete.height = 340;
      return 0;
    }
    case VIDIOC_S_FMT: {
      auto* format = static_cast<v4l2_format*>(arg);
      expect(format->fmt.pix.pixelformat == V4L2_PIX_FMT_GREY, "setup requests GREY");
      ++g_formats_set;
      format->fmt.pix.pixelformat = g_negotiated;
      return 0;
    }
    case VIDIOC_REQBUFS:
      ++g_buffer_requests;
      // Stop the positive controls here: no actual buffers or hardware are needed.
      errno = ENOMEM;
      return -1;
  }
  errno = EINVAL;
  return -1;
}

int testNegotiation() {
  using biopass::CameraCaptureFormat;
  expect(!biopass::openCameraSession(std::nullopt, CameraCaptureFormat::DirectV4L2Grey),
         "direct IR requires an explicit node");
  for (const auto format : {CameraCaptureFormat::DirectV4L2Grey, CameraCaptureFormat::V4L2Grey}) {
    for (const auto negotiated : {V4L2_PIX_FMT_GREY, V4L2_PIX_FMT_YUYV}) {
      g_negotiated = negotiated;
      g_formats_set = g_buffer_requests = 0;
      const auto session = format == CameraCaptureFormat::DirectV4L2Grey
                               ? biopass::openCameraSession(kDevice, format, 0, 100)
                               : biopass::V4l2CaptureSession::open(kDevice, format, 0, 100);
      expect(!session, "fake device either rejects the format or stops at buffer allocation");
      expect(g_formats_set == 1, "setup reaches format negotiation");
      const bool reject =
          format == CameraCaptureFormat::DirectV4L2Grey && negotiated != V4L2_PIX_FMT_GREY;
      expect(g_buffer_requests == (reject ? 0 : 1),
             reject ? "direct IR rejects a driver-adjusted colour format before allocating buffers"
                    : "accepted formats reach buffer allocation (positive control)");
      expect(g_path_fd == -1 && g_capture_fd == -1, "failed setup closes both device descriptors");
    }
  }
  return g_failures;
}
