#include "camera_capture.h"

#include <dirent.h>
#include <libcamera/libcamera.h>
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "camera_lifecycle.h"
#include "frame_sampling.h"
#include "pixel_convert.h"

namespace biopass {

namespace {

// Detection letterboxes to 640px and recognition uses 112x112 crops, so
// anything beyond 720p only costs USB bandwidth. Uncompressed streams are
// capped harder: 1280x720 YUYV (~1.8 MB/frame) runs at a few fps on USB 2.0,
// or truncates once the IR stream shares the bus.
const libcamera::Size kMaxCompressedCaptureSize(1280, 720);
const libcamera::Size kMaxUncompressedCaptureSize(640, 480);
// Narrower frames leave too few pixels for a face at arm's length; prefer a
// size above the cap over one below it.
constexpr unsigned int kMinCaptureWidth = 640;

std::string device_label(const std::optional<std::string>& linux_video_device_path) {
  return linux_video_device_path.has_value() ? *linux_video_device_path : std::string("<default>");
}

// Routes libcamera's logging through spdlog (see setupBiopassLogger in helper.cc).
class SpdlogStreambuf : public std::streambuf {
 public:
  int overflow(int ch) override {
    if (ch == EOF) {
      return ch;
    }
    if (ch == '\n') {
      flushLine();
    } else {
      line_.push_back(static_cast<char>(ch));
    }
    return ch;
  }

 private:
  void flushLine() {
    if (!line_.empty()) {
      if (line_.find(" ERROR ") != std::string::npos ||
          line_.find(" FATAL ") != std::string::npos) {
        spdlog::error("libcamera: {}", line_);
      } else if (line_.find(" WARN ") != std::string::npos) {
        spdlog::warn("libcamera: {}", line_);
      } else {
        spdlog::info("libcamera: {}", line_);
      }
      line_.clear();
    }
  }

  std::string line_;
};

// Process-wide and outlives all sessions; RGB and IR capture run
// concurrently as two Cameras from this one manager.
std::shared_ptr<libcamera::CameraManager> cameraManager() {
  static std::mutex init_mutex;
  static std::shared_ptr<libcamera::CameraManager> manager;

  std::lock_guard<std::mutex> lock(init_mutex);
  if (manager) {
    return manager;
  }

  if (!std::getenv("LIBCAMERA_LOG_LEVELS") && !std::getenv("LIBCAMERA_LOG_FILE")) {
    static SpdlogStreambuf log_streambuf;
    static std::ostream log_stream(&log_streambuf);
    libcamera::logSetStream(&log_stream);
  }

  auto candidate = std::make_shared<libcamera::CameraManager>();
  if (candidate->start() != 0) {
    spdlog::error("FaceAuth: Failed to start libcamera CameraManager");
    return nullptr;
  }

  manager = std::move(candidate);
  return manager;
}

std::shared_ptr<libcamera::Camera> findCameraByPath(libcamera::CameraManager& manager,
                                                    const std::string& linux_video_device_path) {
  struct stat device_info{};
  if (stat(linux_video_device_path.c_str(), &device_info) != 0 || !S_ISCHR(device_info.st_mode)) {
    return nullptr;
  }

  for (const auto& camera : manager.cameras()) {
    const auto devices = camera->properties().get(libcamera::properties::SystemDevices);
    if (!devices) {
      continue;
    }
    for (const int64_t device : *devices) {
      if (device == static_cast<int64_t>(device_info.st_rdev)) {
        return camera;
      }
    }
  }
  return nullptr;
}

std::shared_ptr<libcamera::Camera> findCamera(
    libcamera::CameraManager& manager, const std::optional<std::string>& linux_video_device_path) {
  if (linux_video_device_path.has_value()) {
    auto camera = findCameraByPath(manager, *linux_video_device_path);
    if (!camera) {
      spdlog::error("FaceAuth: Camera path '{}' was not found among libcamera devices",
                    *linux_video_device_path);
    }
    return camera;
  }

  const auto cameras = manager.cameras();
  if (cameras.empty()) {
    spdlog::error("FaceAuth: No camera devices reported by libcamera");
    return nullptr;
  }
  return cameras.front();
}

// Maps the camera's SystemDevices dev_t entries back to /dev/video* paths
// (field debugging via listCameraDevices).
std::vector<std::string> videoPathsForCamera(const libcamera::Camera& camera) {
  std::vector<std::string> paths;
  const auto devices = camera.properties().get(libcamera::properties::SystemDevices);
  if (!devices) {
    return paths;
  }

  DIR* dir = opendir("/dev");
  if (!dir) {
    return paths;
  }
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    const std::string name(entry->d_name);
    if (name.rfind("video", 0) != 0) {
      continue;
    }
    const std::string path = "/dev/" + name;
    struct stat st{};
    if (stat(path.c_str(), &st) != 0 || !S_ISCHR(st.st_mode)) {
      continue;
    }
    for (const int64_t device : *devices) {
      if (device == static_cast<int64_t>(st.st_rdev)) {
        paths.push_back(path);
        break;
      }
    }
  }
  closedir(dir);
  std::sort(paths.begin(), paths.end());
  return paths;
}

// Packed RGB formats produced by libcamera's SoftISP (simple pipeline, e.g.
// IPU6 MIPI laptop cameras). See PackedRgbLayout for the byte order.
std::optional<PackedRgbLayout> packedRgbLayout(const libcamera::PixelFormat& format) {
  if (format == libcamera::formats::RGB888) {
    return PackedRgbLayout::kBgr24;
  }
  if (format == libcamera::formats::BGR888) {
    return PackedRgbLayout::kRgb24;
  }
  if (format == libcamera::formats::XRGB8888 || format == libcamera::formats::ARGB8888) {
    return PackedRgbLayout::kBgrx32;
  }
  if (format == libcamera::formats::XBGR8888 || format == libcamera::formats::ABGR8888) {
    return PackedRgbLayout::kRgbx32;
  }
  return std::nullopt;
}

bool isSupportedPixelFormat(const libcamera::PixelFormat& format) {
  return format == libcamera::formats::YUYV || format == libcamera::formats::MJPEG ||
         format == libcamera::formats::R8 || packedRgbLayout(format).has_value();
}

std::string listAvailableFormats(const libcamera::StreamFormats& formats) {
  std::ostringstream oss;
  bool first = true;
  for (const auto& format : formats.pixelformats()) {
    if (!first) {
      oss << ", ";
    }
    oss << format.toString();
    first = false;
  }
  return oss.str();
}

// Mean brightness in [0, 255], sampling every 4th pixel; enough to tell a
// lit IR frame from a dark one.
double meanLuma(const ImageRGB& image) {
  const size_t pixels = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
  if (pixels == 0 || image.data.size() < pixels * 3) {
    return 0.0;
  }
  const uint8_t* data = image.data.data();
  uint64_t sum = 0;
  size_t count = 0;
  for (size_t i = 0; i < pixels; i += 4, ++count) {
    sum += data[i * 3] + data[i * 3 + 1] + data[i * 3 + 2];
  }
  return static_cast<double>(sum) / (3.0 * static_cast<double>(count));
}

// Some emitters also produce washed-out (near all-white) frames, which hold no
// more face detail than dark ones. Above this mean, a frame counts as
// over-exposed and only wins when every sample is.
constexpr double kIrOverexposedMeanLuma = 220.0;

// Ranks an IR sample by its mean brightness: the brightest well-exposed frame
// first, then over-exposed frames from the least washed-out.
double irFrameScore(double mean) { return mean <= kIrOverexposedMeanLuma ? mean : -mean; }

// Dropped (failed or unconvertible) frames one capture tolerates before
// giving up: about a second at 30 fps. Bounds the loop when there is no
// capture deadline.
constexpr int kMaxDroppedFrames = 30;

uint64_t area(const libcamera::Size& size) {
  return static_cast<uint64_t>(size.width) * static_cast<uint64_t>(size.height);
}

// Largest size within the format's cap, unless that is narrower than
// kMinCaptureWidth and a larger one exists; then the smallest above the cap.
// nullopt when the camera reports no discrete sizes.
std::optional<libcamera::Size> pickCaptureSize(const std::vector<libcamera::Size>& sizes,
                                               const libcamera::PixelFormat& pixel_format) {
  const libcamera::Size& max_size = pixel_format == libcamera::formats::MJPEG
                                        ? kMaxCompressedCaptureSize
                                        : kMaxUncompressedCaptureSize;

  std::optional<libcamera::Size> best_within;
  std::optional<libcamera::Size> smallest_above;
  for (const auto& size : sizes) {
    if (size.width <= max_size.width && size.height <= max_size.height) {
      if (!best_within || area(size) > area(*best_within)) {
        best_within = size;
      }
    } else if (!smallest_above || area(size) < area(*smallest_above)) {
      smallest_above = size;
    }
  }

  if (best_within && (best_within->width >= kMinCaptureWidth || !smallest_above)) {
    return best_within;
  }
  return smallest_above;
}

// Picks the most preferred format the camera offers, sets a capped size and
// validates the configuration.
bool negotiate(libcamera::CameraConfiguration& config, CameraCaptureFormat requested_format,
               const std::string& camera_label) {
  libcamera::StreamConfiguration& stream_config = config.at(0);
  const auto& formats = stream_config.formats();

  // SoftISP outputs only packed RGB. BGR888 first since its bytes are
  // already in RGB order, then the other 24-bit and 32-bit variants.
  const std::vector<libcamera::PixelFormat> packed_rgb = {
      libcamera::formats::BGR888,   libcamera::formats::RGB888,   libcamera::formats::XBGR8888,
      libcamera::formats::XRGB8888, libcamera::formats::ABGR8888, libcamera::formats::ARGB8888};

  std::vector<libcamera::PixelFormat> preference;
  if (requested_format == CameraCaptureFormat::V4L2Grey) {
    // Prefer a true grey stream, but some laptop IR sensors (e.g. Windows
    // Hello cameras) only expose the IR stream as YUYV/MJPEG/RGB; decode
    // those instead of failing outright.
    preference = {libcamera::formats::R8, libcamera::formats::YUYV, libcamera::formats::MJPEG};
    preference.insert(preference.end(), packed_rgb.begin(), packed_rgb.end());
  } else {
    // Prefer MJPEG for the colour stream: an uncompressed YUYV stream at a
    // webcam's maximum size starves a USB 2.0 bus (slow frame rates or
    // truncated frames, worse when the IR stream is open at the same time).
    preference = {libcamera::formats::MJPEG, libcamera::formats::YUYV};
    preference.insert(preference.end(), packed_rgb.begin(), packed_rgb.end());
    preference.push_back(libcamera::formats::R8);
  }

  const auto available = formats.pixelformats();
  bool picked = false;
  for (const auto& candidate : preference) {
    if (std::find(available.begin(), available.end(), candidate) != available.end()) {
      stream_config.pixelFormat = candidate;
      picked = true;
      break;
    }
  }

  if (!picked) {
    spdlog::error(
        "FaceAuth: Camera '{}' exposes none of the required pixel formats (available: {})",
        camera_label, listAvailableFormats(formats));
    return false;
  }

  const auto sizes = formats.sizes(stream_config.pixelFormat);
  const libcamera::Size full_size = formats.range(stream_config.pixelFormat).max;
  if (packedRgbLayout(stream_config.pixelFormat) && !full_size.isNull()) {
    // SoftISP crops (it does not scale) to the requested output size, so a
    // capped size would narrow the field of view. Take the full sensor frame.
    stream_config.size = full_size;
  } else if (const auto size = pickCaptureSize(sizes, stream_config.pixelFormat)) {
    stream_config.size = *size;
  } else {
    spdlog::warn("FaceAuth: Camera '{}' reports no discrete sizes for {}; using default {}",
                 camera_label, stream_config.pixelFormat.toString(), stream_config.size.toString());
  }
  const libcamera::PixelFormat requested_pixel_format = stream_config.pixelFormat;
  const libcamera::Size requested_size = stream_config.size;

  const auto validation = config.validate();
  if (validation == libcamera::CameraConfiguration::Invalid) {
    spdlog::error("FaceAuth: Invalid camera configuration for '{}'", camera_label);
    return false;
  }
  if (validation == libcamera::CameraConfiguration::Adjusted) {
    spdlog::warn("FaceAuth: Camera '{}' adjusted configuration from {}-{} to {}-{}", camera_label,
                 requested_size.toString(), requested_pixel_format.toString(),
                 stream_config.size.toString(), stream_config.pixelFormat.toString());
  }

  if (!isSupportedPixelFormat(stream_config.pixelFormat)) {
    spdlog::error(
        "FaceAuth: Camera '{}' adjusted configuration to unsupported format {} (available: {})",
        camera_label, stream_config.pixelFormat.toString(), listAvailableFormats(formats));
    return false;
  }

  if (requested_format == CameraCaptureFormat::V4L2Grey &&
      stream_config.pixelFormat != libcamera::formats::R8) {
    spdlog::warn("FaceAuth: Camera '{}' exposes no GREY format; falling back to decoded {} capture",
                 camera_label, stream_config.pixelFormat.toString());
  }

  spdlog::debug("FaceAuth: Camera '{}' configured as {}-{} (available: {})", camera_label,
                stream_config.size.toString(), stream_config.pixelFormat.toString(),
                listAvailableFormats(formats));
  return true;
}

bool convertFrame(const libcamera::PixelFormat& pixel_format, const uint8_t* data,
                  size_t bytes_used, int width, int height, int stride, ImageRGB& out) {
  if (pixel_format == libcamera::formats::YUYV) {
    return yuyvToRgb(data, bytes_used, width, height, stride, out);
  }
  if (pixel_format == libcamera::formats::R8) {
    return greyToRgb(data, bytes_used, width, height, stride, out);
  }
  if (pixel_format == libcamera::formats::MJPEG) {
    return mjpegToRgb(data, bytes_used, out);
  }
  if (const auto layout = packedRgbLayout(pixel_format)) {
    return packedRgbToRgb(data, bytes_used, width, height, stride, *layout, out);
  }
  return false;
}

class LibcameraCaptureSession : public ICameraCaptureSession {
 public:
  static std::unique_ptr<LibcameraCaptureSession> open(
      std::shared_ptr<libcamera::CameraManager> manager, std::shared_ptr<libcamera::Camera> camera,
      CameraCaptureFormat requested_format, std::string camera_label, int warmup_frames,
      int capture_timeout_ms) {
    auto session = std::unique_ptr<LibcameraCaptureSession>(
        new LibcameraCaptureSession(std::move(manager), std::move(camera), requested_format,
                                    std::move(camera_label), warmup_frames, capture_timeout_ms));
    if (!session->setup()) {
      return nullptr;
    }
    return session;
  }

  ~LibcameraCaptureSession() override { close(); }

  bool isOpen() const override { return lifecycle_.isStarted() && !disconnected_; }

  ImageRGB capture() override {
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

    // Colour sessions warm up once; grey/IR sessions warm up on every capture
    // to let the IR emitter and auto-exposure settle.
    const int discard_count = (is_grey_ || !warmed_up_) ? warmup_frames_ : 0;
    warmed_up_ = true;
    for (int i = 0; i < discard_count; ++i) {
      libcamera::Request* request = waitForRequest(deadline, has_timeout);
      if (!request) {
        return {};
      }
      if (!requeue(request)) {
        close();
        return {};
      }
    }

    // Many IR emitters strobe lit/dark, and a fixed warmup count lands on the
    // same phase every time, so sample several frames and keep the brightest
    // one that isn't over-exposed.
    const int samples = is_grey_ ? kIrCaptureSampleFrames : 1;
    FrameSampling sampling(samples, kMaxDroppedFrames);
    ImageRGB best;
    double best_mean = 0.0;
    while (sampling.wantsMore()) {
      libcamera::Request* request = waitForRequest(deadline, has_timeout);
      if (!request) {
        break;
      }
      ImageRGB image;
      const bool ok = frameSucceeded(request) && extractFrame(request, image);
      if (!requeue(request)) {
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
      spdlog::error("FaceAuth: No usable frame from '{}' after {} dropped frames", camera_label_,
                    kMaxDroppedFrames);
    }
    if (samples > 1 && !best.empty()) {
      spdlog::debug("FaceAuth: Kept brightest of {} frames from '{}' (mean={:.1f})", samples,
                    camera_label_, best_mean);
    }
    return best;
  }

 private:
  LibcameraCaptureSession(std::shared_ptr<libcamera::CameraManager> manager,
                          std::shared_ptr<libcamera::Camera> camera,
                          CameraCaptureFormat requested_format, std::string camera_label,
                          int warmup_frames, int capture_timeout_ms)
      : manager_(std::move(manager)),
        camera_(std::move(camera)),
        camera_label_(std::move(camera_label)),
        is_grey_(requested_format == CameraCaptureFormat::V4L2Grey),
        warmup_frames_(std::max(0, warmup_frames)),
        capture_timeout_ms_(capture_timeout_ms) {}

  bool setup() {
    config_ = camera_->generateConfiguration({libcamera::StreamRole::StillCapture});
    if (!config_ || config_->size() == 0) {
      spdlog::error("FaceAuth: Failed to generate camera configuration for '{}'", camera_label_);
      return false;
    }

    if (!negotiate(*config_,
                   is_grey_ ? CameraCaptureFormat::V4L2Grey : CameraCaptureFormat::Default,
                   camera_label_)) {
      return false;
    }

    if (camera_->acquire() != 0) {
      spdlog::error("FaceAuth: Failed to acquire camera '{}'", camera_label_);
      return false;
    }
    lifecycle_.markAcquired();

    if (camera_->configure(config_.get()) != 0) {
      spdlog::error("FaceAuth: Failed to configure camera '{}'", camera_label_);
      return false;
    }

    libcamera::StreamConfiguration& stream_config = config_->at(0);
    pixel_format_ = stream_config.pixelFormat;
    width_ = static_cast<int>(stream_config.size.width);
    height_ = static_cast<int>(stream_config.size.height);
    stride_ = static_cast<int>(stream_config.stride);
    stream_ = stream_config.stream();

    allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
    if (allocator_->allocate(stream_) < 0) {
      spdlog::error("FaceAuth: Failed to allocate buffers for '{}'", camera_label_);
      return false;
    }

    for (const auto& buffer : allocator_->buffers(stream_)) {
      auto request = camera_->createRequest();
      if (!request || request->addBuffer(stream_, buffer.get()) < 0) {
        spdlog::error("FaceAuth: Failed to create capture request for '{}'", camera_label_);
        return false;
      }
      if (!mapBuffer(buffer.get())) {
        spdlog::error("FaceAuth: Failed to mmap buffer for '{}'", camera_label_);
        return false;
      }
      requests_.push_back(std::move(request));
    }

    camera_->requestCompleted.connect(&connection_token_, [this](libcamera::Request* request) {
      if (request->status() == libcamera::Request::RequestCancelled) {
        return;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_.push_back(request);
      }
      ready_.notify_one();
    });
    camera_->disconnected.connect(&connection_token_, [this]() {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        disconnected_ = true;
      }
      ready_.notify_all();
    });

    if (camera_->start() < 0) {
      spdlog::error("FaceAuth: Failed to start camera '{}'", camera_label_);
      camera_->requestCompleted.disconnect(&connection_token_);
      return false;
    }
    lifecycle_.markStarted();

    for (auto& request : requests_) {
      if (camera_->queueRequest(request.get()) < 0) {
        // The camera owns the requests already queued. close() (via the
        // destructor) stops it; the failure may be an unplug, where stop()
        // cancels nothing, so the buffers are kept.
        spdlog::error("FaceAuth: Failed to queue initial request for '{}'", camera_label_);
        lifecycle_.markRequestsHeld();
        return false;
      }
    }

    return true;
  }

  bool mapBuffer(libcamera::FrameBuffer* buffer) {
    const auto planes = buffer->planes();
    if (planes.empty()) {
      return false;
    }
    const auto& plane = planes[0];
    const size_t mapping_size = plane.offset + plane.length;
    void* mapping = mmap(nullptr, mapping_size, PROT_READ, MAP_SHARED, plane.fd.get(), 0);
    if (mapping == MAP_FAILED) {
      return false;
    }
    mappings_[buffer] = Mapping{mapping, mapping_size, plane.offset};
    return true;
  }

  libcamera::Request* waitForRequest(std::chrono::steady_clock::time_point deadline,
                                     bool has_timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [this] { return !completed_.empty() || disconnected_; };
    bool have_request;
    if (has_timeout) {
      have_request = ready_.wait_until(lock, deadline, ready);
    } else {
      ready_.wait(lock, ready);
      have_request = true;
    }
    if (have_request && completed_.empty()) {
      spdlog::error("FaceAuth: Camera '{}' was disconnected", camera_label_);
      lock.unlock();
      close();
      return nullptr;
    }
    if (!have_request) {
      spdlog::error("FaceAuth: Timed out waiting for frame from '{}'", camera_label_);
      lock.unlock();
      close();
      return nullptr;
    }
    libcamera::Request* request = completed_.front();
    completed_.pop_front();
    return request;
  }

  // Requeues every completed request. False if one couldn't be requeued; the
  // caller must then close() (outside the lock).
  bool drainPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!completed_.empty()) {
      libcamera::Request* request = completed_.front();
      completed_.pop_front();
      if (!requeue(request)) {
        return false;
      }
    }
    return true;
  }

  // A request that can't be requeued is lost, so the session would slowly run
  // out of requests and time out. Callers treat this as a capture failure.
  bool requeue(libcamera::Request* request) {
    request->reuse(libcamera::Request::ReuseBuffers);
    const int ret = camera_->queueRequest(request);
    if (ret < 0) {
      // May be an unplug whose disconnected signal hasn't fired yet.
      spdlog::error("FaceAuth: Failed to requeue request for '{}': {}", camera_label_, ret);
      lifecycle_.markRequestsHeld();
      return false;
    }
    return true;
  }

  // An errored or cancelled buffer holds partial or stale data.
  bool frameSucceeded(libcamera::Request* request) {
    libcamera::FrameBuffer* buffer = request->findBuffer(stream_);
    if (buffer && buffer->metadata().status == libcamera::FrameMetadata::FrameSuccess) {
      return true;
    }
    spdlog::debug("FaceAuth: Dropping frame with status {} from '{}'",
                  buffer ? static_cast<int>(buffer->metadata().status) : -1, camera_label_);
    return false;
  }

  // Call only after frameSucceeded(), which checks the buffer exists.
  bool extractFrame(libcamera::Request* request, ImageRGB& out) {
    libcamera::FrameBuffer* buffer = request->findBuffer(stream_);
    const auto it = mappings_.find(buffer);
    const auto metadata = buffer->metadata().planes();
    if (it == mappings_.end() || metadata.empty()) {
      spdlog::error("FaceAuth: Frame from '{}' has no mapped data", camera_label_);
      return false;
    }
    const size_t bytes_used = metadata[0].bytesused;
    const uint8_t* data = static_cast<const uint8_t*>(it->second.base) + it->second.plane_offset;
    if (!convertFrame(pixel_format_, data, bytes_used, width_, height_, stride_, out)) {
      spdlog::error("FaceAuth: Failed to convert frame from '{}'", camera_label_);
      return false;
    }
    return true;
  }

  // After a real stop() every request has completed or been cancelled.
  bool anyRequestPending() const {
    return std::any_of(requests_.begin(), requests_.end(), [](const auto& request) {
      return request->status() == libcamera::Request::RequestPending;
    });
  }

  // Safe to call more than once.
  void close() {
    // After an unplug stop() is a no-op that leaves requests in the pipeline.
    if (disconnected_) {
      lifecycle_.markDisconnected();
    }
    if (lifecycle_.needsStop()) {
      const int ret = camera_->stop();
      if (ret < 0) {
        spdlog::error("FaceAuth: Failed to stop camera '{}': {}", camera_label_, ret);
      }
      // libcamera marks an unplugged camera stopped before it signals, so
      // stop() may have cancelled nothing; a request still pending is held.
      if (disconnected_ || anyRequestPending()) {
        lifecycle_.markRequestsHeld();
      }
      lifecycle_.markStopped(ret == 0);
    }
    camera_->requestCompleted.disconnect(&connection_token_);
    camera_->disconnected.disconnect(&connection_token_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      completed_.clear();
    }
    for (auto& [buffer, mapping] : mappings_) {
      munmap(mapping.base, mapping.size);
    }
    mappings_.clear();
    if (lifecycle_.canFreeBuffers()) {
      requests_.clear();
      allocator_.reset();
    } else {
      // libcamera may still hold these; leaking beats a use-after-free.
      for (auto& request : requests_) {
        (void)request.release();
      }
      requests_.clear();
      (void)allocator_.release();
    }
    if (lifecycle_.needsRelease()) {
      camera_->release();
      lifecycle_.markReleased();
    }
  }

  struct Mapping {
    void* base = nullptr;
    size_t size = 0;
    size_t plane_offset = 0;
  };

  std::shared_ptr<libcamera::CameraManager> manager_;
  std::shared_ptr<libcamera::Camera> camera_;
  std::string camera_label_;
  bool is_grey_ = false;
  int warmup_frames_ = 0;
  int capture_timeout_ms_ = 0;
  bool warmed_up_ = false;

  std::unique_ptr<libcamera::CameraConfiguration> config_;
  libcamera::Stream* stream_ = nullptr;
  libcamera::PixelFormat pixel_format_;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;

  std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;
  std::vector<std::unique_ptr<libcamera::Request>> requests_;
  std::map<libcamera::FrameBuffer*, Mapping> mappings_;

  std::mutex mutex_;
  std::condition_variable ready_;
  std::deque<libcamera::Request*> completed_;
  int connection_token_ = 0;
  // Set from libcamera's thread when the camera is unplugged.
  std::atomic<bool> disconnected_{false};

  CameraLifecycle lifecycle_;
};

}  // namespace

bool checkCameraAvailability(const std::optional<std::string>& linux_video_device_path) {
  auto session = openCameraSession(linux_video_device_path);
  return session && session->isOpen();
}

std::unique_ptr<ICameraCaptureSession> openCameraSession(
    const std::optional<std::string>& linux_video_device_path, CameraCaptureFormat format,
    int warmup_frames, int capture_timeout_ms) {
  auto manager = cameraManager();
  if (!manager) {
    return nullptr;
  }

  auto camera = findCamera(*manager, linux_video_device_path);
  if (!camera) {
    return nullptr;
  }

  return LibcameraCaptureSession::open(manager, camera, format,
                                       device_label(linux_video_device_path), warmup_frames,
                                       capture_timeout_ms);
}

ImageRGB captureImageByIRCamera(const std::string& device_path, int warmup_frames,
                                int capture_timeout_ms) {
  if (device_path.empty()) {
    spdlog::error("FaceAuth: IR camera capture requires a /dev/video* path");
    return {};
  }

  auto session = openCameraSession(device_path, CameraCaptureFormat::V4L2Grey, warmup_frames,
                                   capture_timeout_ms);
  if (!session) {
    return {};
  }

  return session->capture();
}

std::vector<CameraDeviceInfo> listCameraDevices() {
  std::vector<CameraDeviceInfo> result;
  auto manager = cameraManager();
  if (!manager) {
    return result;
  }

  for (const auto& camera : manager->cameras()) {
    CameraDeviceInfo info;
    info.id = camera->id();
    const auto model = camera->properties().get(libcamera::properties::Model);
    info.model = model ? std::string(*model) : info.id;
    info.video_paths = videoPathsForCamera(*camera);
    result.push_back(std::move(info));
  }
  return result;
}

std::vector<CameraFormatDesc> listCameraFormats(const std::string& device_path) {
  std::vector<CameraFormatDesc> result;
  auto manager = cameraManager();
  if (!manager) {
    return result;
  }

  auto camera = findCameraByPath(*manager, device_path);
  if (!camera) {
    return result;
  }

  auto config = camera->generateConfiguration({libcamera::StreamRole::StillCapture});
  if (!config || config->size() == 0) {
    return result;
  }

  const auto& formats = config->at(0).formats();
  for (const auto& pixel_format : formats.pixelformats()) {
    CameraFormatDesc desc;
    desc.pixel_format = pixel_format.toString();
    desc.supported = isSupportedPixelFormat(pixel_format);
    for (const auto& size : formats.sizes(pixel_format)) {
      desc.sizes.emplace_back(static_cast<int>(size.width), static_cast<int>(size.height));
    }
    result.push_back(std::move(desc));
  }
  return result;
}

}  // namespace biopass
