#include "v4l2_capture.h"

#include <fcntl.h>
#include <libcamera/formats.h>
#include <libcamera/geometry.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>

#include "capture_policy.h"
#include "frame_sampling.h"
#include "pixel_convert.h"

namespace biopass {

namespace {

constexpr uint32_t kBufferCount = 4;
// VIDEO_MAJOR in the kernel: /dev/video*, also v4l-subdev/vbi/radio (which
// QUERYCAP then rejects).
constexpr unsigned int kVideoMajor = 81;
// Bounds the ENUM_FMT / ENUM_FRAMESIZES loops against a misbehaving driver.
constexpr uint32_t kMaxEnumEntries = 256;

struct FourccMapping {
  uint32_t fourcc;
  libcamera::PixelFormat pixel_format;
};

// V4L2 names packed RGB by byte order, libcamera by little-endian word, so
// RGB24 (bytes R, G, B) is libcamera BGR888 and BGR24 is RGB888.
const std::vector<FourccMapping>& fourccMappings() {
  static const std::vector<FourccMapping> mappings = {
      {V4L2_PIX_FMT_YUYV, libcamera::formats::YUYV},
      {V4L2_PIX_FMT_MJPEG, libcamera::formats::MJPEG},
      {V4L2_PIX_FMT_GREY, libcamera::formats::R8},
      {V4L2_PIX_FMT_RGB24, libcamera::formats::BGR888},
      {V4L2_PIX_FMT_BGR24, libcamera::formats::RGB888},
  };
  return mappings;
}

std::string fourccToString(uint32_t fourcc) {
  std::string name;
  for (int shift = 0; shift < 32; shift += 8) {
    const char ch = static_cast<char>((fourcc >> shift) & 0xff);
    name.push_back(ch >= 0x20 && ch < 0x7f ? ch : '?');
  }
  return name;
}

std::string listFourccs(const std::vector<uint32_t>& fourccs) {
  std::ostringstream oss;
  for (size_t i = 0; i < fourccs.size(); ++i) {
    oss << (i > 0 ? ", " : "") << fourccToString(fourccs[i]);
  }
  return oss.str();
}

int xioctl(int fd, unsigned long request, void* arg) {
  int ret;
  do {
    ret = ioctl(fd, request, arg);
  } while (ret < 0 && errno == EINTR);
  return ret;
}

// The helper runs as root and the path comes from user config, so only a
// V4L2 char device may be opened: opening some devices has side effects
// (e.g. /dev/watchdog arms the watchdog). The path is pinned with O_PATH
// (which never calls the driver), checked, then reopened through
// /proc/self/fd, so swapping a symlink in between cannot redirect the open.
int openDevice(const std::string& device_path) {
  const int path_fd = ::open(device_path.c_str(), O_PATH | O_CLOEXEC);
  if (path_fd < 0) {
    spdlog::error("FaceAuth: Failed to open camera '{}': {}", device_path, std::strerror(errno));
    return -1;
  }
  struct stat path_info{};
  if (fstat(path_fd, &path_info) != 0 || !isVideoCharDevice(path_info.st_mode, path_info.st_rdev)) {
    spdlog::error("FaceAuth: Camera path '{}' is not a V4L2 video device", device_path);
    ::close(path_fd);
    return -1;
  }

  const std::string pinned_path = "/proc/self/fd/" + std::to_string(path_fd);
  const int fd = ::open(pinned_path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
  const int open_errno = errno;
  ::close(path_fd);
  if (fd < 0) {
    spdlog::error("FaceAuth: Failed to open camera '{}': {}", device_path,
                  std::strerror(open_errno));
    return -1;
  }
  struct stat fd_info{};
  if (fstat(fd, &fd_info) != 0 || !isVideoCharDevice(fd_info.st_mode, fd_info.st_rdev) ||
      fd_info.st_rdev != path_info.st_rdev) {
    spdlog::error("FaceAuth: Camera path '{}' changed while opening it; refusing", device_path);
    ::close(fd);
    return -1;
  }
  return fd;
}

// The node must stream single-planar video capture (metadata and output
// nodes are rejected).
bool isStreamingCapture(int fd, const std::string& device_path) {
  v4l2_capability cap{};
  if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    spdlog::error("FaceAuth: Failed to query camera '{}': {}", device_path, std::strerror(errno));
    return false;
  }
  const uint32_t caps =
      (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
  if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
    spdlog::error("FaceAuth: Camera '{}' is not a V4L2 streaming capture device (caps 0x{:08x})",
                  device_path, caps);
    return false;
  }
  return true;
}

struct V4l2Format {
  uint32_t fourcc = 0;
  std::vector<libcamera::Size> sizes;
  // Set for a stepwise/continuous format (then `sizes` is empty).
  std::optional<libcamera::SizeRange> range;
};

std::vector<V4l2Format> enumerateFormats(int fd) {
  std::vector<V4l2Format> formats;
  for (uint32_t index = 0; index < kMaxEnumEntries; ++index) {
    v4l2_fmtdesc desc{};
    desc.index = index;
    desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_ENUM_FMT, &desc) < 0) {
      break;
    }
    V4l2Format format;
    format.fourcc = desc.pixelformat;
    for (uint32_t size_index = 0; size_index < kMaxEnumEntries; ++size_index) {
      v4l2_frmsizeenum size{};
      size.index = size_index;
      size.pixel_format = desc.pixelformat;
      if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0) {
        break;
      }
      if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
        format.sizes.emplace_back(size.discrete.width, size.discrete.height);
      } else {
        format.range =
            libcamera::SizeRange(libcamera::Size(size.stepwise.min_width, size.stepwise.min_height),
                                 libcamera::Size(size.stepwise.max_width, size.stepwise.max_height),
                                 size.stepwise.step_width, size.stepwise.step_height);
        break;
      }
    }
    formats.push_back(std::move(format));
  }
  return formats;
}

bool convertFrame(uint32_t fourcc, const uint8_t* data, size_t bytes_used, int width, int height,
                  int stride, ImageRGB& out) {
  switch (fourcc) {
    case V4L2_PIX_FMT_YUYV:
      return yuyvToRgb(data, bytes_used, width, height, stride, out);
    case V4L2_PIX_FMT_MJPEG:
      return mjpegToRgb(data, bytes_used, out);
    case V4L2_PIX_FMT_GREY:
      return greyToRgb(data, bytes_used, width, height, stride, out);
    case V4L2_PIX_FMT_RGB24:
      return packedRgbToRgb(data, bytes_used, width, height, stride, PackedRgbLayout::kRgb24, out);
    case V4L2_PIX_FMT_BGR24:
      return packedRgbToRgb(data, bytes_used, width, height, stride, PackedRgbLayout::kBgr24, out);
    default:
      return false;
  }
}

}  // namespace

std::optional<libcamera::PixelFormat> pixelFormatFromV4l2(uint32_t fourcc) {
  for (const auto& mapping : fourccMappings()) {
    if (mapping.fourcc == fourcc) {
      return mapping.pixel_format;
    }
  }
  return std::nullopt;
}

std::optional<uint32_t> pickV4l2Format(const std::vector<uint32_t>& available,
                                       CameraCaptureFormat format) {
  for (const auto& candidate : pixelFormatPreference(format)) {
    for (const uint32_t fourcc : available) {
      if (pixelFormatFromV4l2(fourcc) == candidate) {
        return fourcc;
      }
    }
  }
  return std::nullopt;
}

bool isVideoCharDevice(mode_t mode, dev_t rdev) {
  return S_ISCHR(mode) && major(rdev) == kVideoMajor;
}

uint32_t v4l2BytesPerLine(uint32_t fourcc, uint32_t width, uint32_t reported) {
  if (reported != 0) {
    return reported;
  }
  switch (fourcc) {
    case V4L2_PIX_FMT_YUYV:
      return width * 2;
    case V4L2_PIX_FMT_GREY:
      return width;
    case V4L2_PIX_FMT_RGB24:
    case V4L2_PIX_FMT_BGR24:
      return width * 3;
    default:
      return 0;
  }
}

std::unique_ptr<V4l2CaptureSession> V4l2CaptureSession::open(const std::string& device_path,
                                                             CameraCaptureFormat requested_format,
                                                             int warmup_frames,
                                                             int capture_timeout_ms) {
  auto session = std::unique_ptr<V4l2CaptureSession>(
      new V4l2CaptureSession(device_path, requested_format, warmup_frames, capture_timeout_ms));
  if (!session->setup(requested_format)) {
    return nullptr;
  }
  return session;
}

V4l2CaptureSession::V4l2CaptureSession(std::string device_path,
                                       CameraCaptureFormat requested_format, int warmup_frames,
                                       int capture_timeout_ms)
    : device_path_(std::move(device_path)),
      is_grey_(requested_format == CameraCaptureFormat::V4L2Grey),
      warmup_frames_(std::max(0, warmup_frames)),
      capture_timeout_ms_(capture_timeout_ms) {}

bool V4l2CaptureSession::setup(CameraCaptureFormat requested_format) {
  fd_ = openDevice(device_path_);
  if (fd_ < 0 || !isStreamingCapture(fd_, device_path_)) {
    return false;
  }

  const auto formats = enumerateFormats(fd_);
  std::vector<uint32_t> available;
  for (const auto& format : formats) {
    available.push_back(format.fourcc);
  }
  const auto picked = pickV4l2Format(available, requested_format);
  if (!picked) {
    spdlog::error(
        "FaceAuth: Camera '{}' exposes none of the required pixel formats (available: {})",
        device_path_, listFourccs(available));
    return false;
  }
  const auto& picked_format =
      *std::find_if(formats.begin(), formats.end(),
                    [&](const V4l2Format& format) { return format.fourcc == *picked; });

  v4l2_format fmt{};
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  const libcamera::PixelFormat pixel_format = *pixelFormatFromV4l2(*picked);
  // v4l2loopback without a producer offers up to 8192x8192, so a range is
  // capped like a discrete list rather than taken at its maximum.
  const auto size = picked_format.range ? pickCaptureSizeInRange(*picked_format.range, pixel_format)
                                        : pickCaptureSize(picked_format.sizes, pixel_format);
  if (size) {
    fmt.fmt.pix.width = size->width;
    fmt.fmt.pix.height = size->height;
  } else {
    if (xioctl(fd_, VIDIOC_G_FMT, &fmt) < 0) {
      spdlog::error("FaceAuth: Failed to read format of camera '{}': {}", device_path_,
                    std::strerror(errno));
      return false;
    }
    spdlog::warn("FaceAuth: Camera '{}' reports no usable sizes for {}; using default {}x{}",
                 device_path_, fourccToString(*picked), fmt.fmt.pix.width, fmt.fmt.pix.height);
  }
  fmt.fmt.pix.pixelformat = *picked;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;
  const uint32_t requested_width = fmt.fmt.pix.width;
  const uint32_t requested_height = fmt.fmt.pix.height;

  if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
    spdlog::error("FaceAuth: Failed to configure camera '{}': {}", device_path_,
                  std::strerror(errno));
    return false;
  }
  if (!pixelFormatFromV4l2(fmt.fmt.pix.pixelformat)) {
    spdlog::error(
        "FaceAuth: Camera '{}' adjusted configuration to unsupported format {} (available: {})",
        device_path_, fourccToString(fmt.fmt.pix.pixelformat), listFourccs(available));
    return false;
  }
  if (fmt.fmt.pix.pixelformat != *picked || fmt.fmt.pix.width != requested_width ||
      fmt.fmt.pix.height != requested_height) {
    spdlog::warn("FaceAuth: Camera '{}' adjusted configuration from {}x{}-{} to {}x{}-{}",
                 device_path_, requested_width, requested_height, fourccToString(*picked),
                 fmt.fmt.pix.width, fmt.fmt.pix.height, fourccToString(fmt.fmt.pix.pixelformat));
  }

  fourcc_ = fmt.fmt.pix.pixelformat;
  width_ = static_cast<int>(fmt.fmt.pix.width);
  height_ = static_cast<int>(fmt.fmt.pix.height);
  stride_ =
      static_cast<int>(v4l2BytesPerLine(fourcc_, fmt.fmt.pix.width, fmt.fmt.pix.bytesperline));

  if (is_grey_ && fourcc_ != V4L2_PIX_FMT_GREY) {
    spdlog::warn("FaceAuth: Camera '{}' exposes no GREY format; falling back to decoded {} capture",
                 device_path_, fourccToString(fourcc_));
  }

  if (!startStreaming()) {
    return false;
  }
  spdlog::debug("FaceAuth: Camera '{}' configured as {}x{}-{} via V4L2 (available: {})",
                device_path_, width_, height_, fourccToString(fourcc_), listFourccs(available));
  return true;
}

bool V4l2CaptureSession::startStreaming() {
  v4l2_requestbuffers request{};
  request.count = kBufferCount;
  request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  request.memory = V4L2_MEMORY_MMAP;
  if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
    spdlog::error("FaceAuth: Failed to allocate buffers for '{}': {}", device_path_,
                  std::strerror(errno));
    return false;
  }
  if (request.count == 0) {
    spdlog::error("FaceAuth: Camera '{}' granted no buffers", device_path_);
    return false;
  }
  buffers_requested_ = true;

  for (uint32_t index = 0; index < request.count; ++index) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    buffer.index = index;
    if (xioctl(fd_, VIDIOC_QUERYBUF, &buffer) < 0) {
      spdlog::error("FaceAuth: Failed to query buffer for '{}': {}", device_path_,
                    std::strerror(errno));
      return false;
    }
    void* data = mmap(nullptr, buffer.length, PROT_READ, MAP_SHARED, fd_, buffer.m.offset);
    if (data == MAP_FAILED) {
      spdlog::error("FaceAuth: Failed to mmap buffer for '{}'", device_path_);
      return false;
    }
    buffers_.push_back(Buffer{data, buffer.length});
  }

  for (uint32_t index = 0; index < buffers_.size(); ++index) {
    if (!requeue(index)) {
      return false;
    }
  }

  int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
    spdlog::error("FaceAuth: Failed to start camera '{}': {}", device_path_, std::strerror(errno));
    return false;
  }
  streaming_ = true;
  return true;
}

ImageRGB V4l2CaptureSession::capture() {
  if (!isOpen()) {
    return {};
  }

  const bool has_timeout = capture_timeout_ms_ > 0;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(std::max(0, capture_timeout_ms_));

  // A long-idle session must never return a stale frame.
  if (!drainPending()) {
    close();
    return {};
  }

  // Same rules as LibcameraCaptureSession: colour warms up once, grey/IR on
  // every capture.
  const int discard_count = (is_grey_ || !warmed_up_) ? warmup_frames_ : 0;
  warmed_up_ = true;
  for (int i = 0; i < discard_count; ++i) {
    Dequeued frame;
    if (!waitForBuffer(deadline, has_timeout, frame)) {
      return {};
    }
    if (!requeue(frame.index)) {
      close();
      return {};
    }
  }

  // Keep the brightest non-over-exposed of several IR frames (strobing
  // emitters), as the libcamera path does.
  const int samples = is_grey_ ? kIrCaptureSampleFrames : 1;
  FrameSampling sampling(samples, kMaxDroppedFrames);
  ImageRGB best;
  double best_mean = 0.0;
  while (sampling.wantsMore()) {
    Dequeued frame;
    if (!waitForBuffer(deadline, has_timeout, frame)) {
      break;
    }
    ImageRGB image;
    const bool ok = frameUsable(frame) && extractFrame(frame, image);
    if (!requeue(frame.index)) {
      close();
      return {};
    }
    if (!ok) {
      sampling.recordDropped();
      continue;
    }
    sampling.recordGood();
    const double mean = samples > 1 ? meanLuma(image) : 0.0;
    if (best.empty() || irFrameScore(mean) > irFrameScore(best_mean)) {
      best_mean = mean;
      best = std::move(image);
    }
  }
  if (!sampling.gotAny() && isOpen()) {
    spdlog::error("FaceAuth: No usable frame from '{}' after {} dropped frames", device_path_,
                  kMaxDroppedFrames);
  }
  if (samples > 1 && !best.empty()) {
    spdlog::debug("FaceAuth: Kept brightest of {} frames from '{}' (mean={:.1f})", samples,
                  device_path_, best_mean);
  }
  return best;
}

// False after closing the session (timeout, disconnect or ioctl error).
bool V4l2CaptureSession::waitForBuffer(std::chrono::steady_clock::time_point deadline,
                                       bool has_timeout, Dequeued& out) {
  while (true) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &buffer) == 0) {
      if (buffer.index >= buffers_.size()) {
        spdlog::error("FaceAuth: Camera '{}' returned invalid buffer {}", device_path_,
                      buffer.index);
        close();
        return false;
      }
      out.index = buffer.index;
      out.bytes_used = std::min<size_t>(buffer.bytesused, buffers_[buffer.index].length);
      out.error = (buffer.flags & V4L2_BUF_FLAG_ERROR) != 0;
      return true;
    }
    if (errno == ENODEV) {
      spdlog::error("FaceAuth: Camera '{}' was disconnected", device_path_);
      close();
      return false;
    }
    if (errno != EAGAIN) {
      spdlog::error("FaceAuth: Failed to dequeue frame from '{}': {}", device_path_,
                    std::strerror(errno));
      close();
      return false;
    }

    int timeout_ms = -1;
    if (has_timeout) {
      const auto remaining =
          std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
      timeout_ms = static_cast<int>(std::max<int64_t>(0, remaining.count()));
    }
    pollfd pfd{fd_, POLLIN, 0};
    const int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0 && errno == EINTR) {
      continue;
    }
    if (ret < 0) {
      spdlog::error("FaceAuth: Failed to poll camera '{}': {}", device_path_, std::strerror(errno));
      close();
      return false;
    }
    if (ret == 0) {
      spdlog::error("FaceAuth: Timed out waiting for frame from '{}'", device_path_);
      close();
      return false;
    }
    if (!(pfd.revents & POLLIN) && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
      spdlog::error("FaceAuth: Camera '{}' was disconnected", device_path_);
      close();
      return false;
    }
  }
}

// Requeues every frame already filled. False on error; the caller closes.
bool V4l2CaptureSession::drainPending() {
  for (size_t i = 0; i < buffers_.size(); ++i) {
    v4l2_buffer buffer{};
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_DQBUF, &buffer) < 0) {
      if (errno == EAGAIN) {
        return true;
      }
      spdlog::error("FaceAuth: Failed to drain frames from '{}': {}", device_path_,
                    std::strerror(errno));
      return false;
    }
    if (!requeue(buffer.index)) {
      return false;
    }
  }
  return true;
}

bool V4l2CaptureSession::requeue(uint32_t index) {
  v4l2_buffer buffer{};
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  buffer.index = index;
  if (xioctl(fd_, VIDIOC_QBUF, &buffer) < 0) {
    spdlog::error("FaceAuth: Failed to requeue buffer for '{}': {}", device_path_,
                  std::strerror(errno));
    return false;
  }
  return true;
}

// An errored or empty buffer holds partial or stale data.
bool V4l2CaptureSession::frameUsable(const Dequeued& frame) const {
  if (!frame.error && frame.bytes_used > 0) {
    return true;
  }
  spdlog::debug("FaceAuth: Dropping frame (error={}, bytesused={}) from '{}'", frame.error,
                frame.bytes_used, device_path_);
  return false;
}

bool V4l2CaptureSession::extractFrame(const Dequeued& frame, ImageRGB& out) {
  const auto* data = static_cast<const uint8_t*>(buffers_[frame.index].data);
  if (!convertFrame(fourcc_, data, frame.bytes_used, width_, height_, stride_, out)) {
    spdlog::error("FaceAuth: Failed to convert frame from '{}'", device_path_);
    return false;
  }
  // Only MJPEG can differ: it decodes at its own header size.
  if ((out.width != width_ || out.height != height_) && !warned_frame_size_) {
    warned_frame_size_ = true;
    spdlog::warn("FaceAuth: Camera '{}' sends {}x{} frames, configured {}x{}", device_path_,
                 out.width, out.height, width_, height_);
  }
  return true;
}

void V4l2CaptureSession::close() {
  if (fd_ < 0) {
    return;
  }
  // After an unplug these ioctls fail; closing the fd still frees everything.
  if (streaming_) {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMOFF, &type) < 0) {
      spdlog::debug("FaceAuth: STREAMOFF failed for '{}': {}", device_path_, std::strerror(errno));
    }
    streaming_ = false;
  }
  for (const auto& buffer : buffers_) {
    munmap(buffer.data, buffer.length);
  }
  buffers_.clear();
  if (buffers_requested_) {
    v4l2_requestbuffers request{};
    request.count = 0;
    request.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    request.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &request) < 0) {
      spdlog::debug("FaceAuth: Failed to free buffers for '{}': {}", device_path_,
                    std::strerror(errno));
    }
    buffers_requested_ = false;
  }
  ::close(fd_);
  fd_ = -1;
}

std::vector<CameraFormatDesc> listV4l2Formats(const std::string& device_path) {
  std::vector<CameraFormatDesc> result;
  const int fd = openDevice(device_path);
  if (fd < 0) {
    return result;
  }
  if (isStreamingCapture(fd, device_path)) {
    for (const auto& format : enumerateFormats(fd)) {
      CameraFormatDesc desc;
      const auto pixel_format = pixelFormatFromV4l2(format.fourcc);
      desc.pixel_format = pixel_format ? pixel_format->toString() : fourccToString(format.fourcc);
      desc.supported = pixel_format.has_value();
      for (const auto& size : format.sizes) {
        desc.sizes.emplace_back(static_cast<int>(size.width), static_cast<int>(size.height));
      }
      if (format.range) {
        // A range shows as its min and max.
        for (const auto& size : {format.range->min, format.range->max}) {
          desc.sizes.emplace_back(static_cast<int>(size.width), static_cast<int>(size.height));
        }
      }
      result.push_back(std::move(desc));
    }
  }
  ::close(fd);
  return result;
}

}  // namespace biopass
