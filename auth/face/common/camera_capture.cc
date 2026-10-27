#include "camera_capture.h"

#include <dirent.h>
#include <libcamera/libcamera.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <algorithm>
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

#include "pixel_convert.h"

namespace biopass {

namespace {


std::string device_label(const std::optional<std::string>& linux_video_device_path) {
  return linux_video_device_path.has_value() ? *linux_video_device_path : std::string("<default>");
}

// Routes libcamera's internal logging through spdlog so it lands wherever
// the rest of the process's logs go (see setupBiopassLogger in helper.cc).
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

// The manager (and its logging redirection) is process-wide and outlives all
// sessions; RGB and IR capture can run concurrently against two Camera
// objects from a single CameraManager.
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

// Reverse-maps a libcamera Camera's SystemDevices dev_t entries back to
// /dev/video* node paths, for field-debugging output (listCameraDevices).
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

// Mean brightness in [0,255]. Subsamples every 4th pixel; plenty for AE
// convergence and strobe-phase decisions and cheap on 720p frames.
double meanLuma(const ImageRGB& img) {
  const size_t n = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
  if (n == 0) return 0.0;
  const uint8_t* p = img.ptr();
  uint64_t sum = 0;
  size_t count = 0;
  for (size_t i = 0; i < n; i += 4, ++count) {
    sum += p[i * 3] + p[i * 3 + 1] + p[i * 3 + 2];
  }
  return static_cast<double>(sum) / (3.0 * static_cast<double>(count));
}

// Auto-exposure is considered converged when the last kAeWindow warm-up
// frames differ by no more than kAeSettleTolerance brightness levels.
// Slow USB 2.0 cameras ramp exposure ~1-2 levels per frame at 10 fps, so the
// window must be wide enough that a ramp cannot hide inside the tolerance.
constexpr int kAeWindow = 4;
constexpr double kAeSettleTolerance = 2.0;
// Cameras start at minimum exposure and sit on a flat near-black plateau for
// a few frames before AE starts ramping, which looks "stable". Do not trust
// stability before this much time has passed or while the image is unusably
// dark; in both cases keep warming up to the cap.
constexpr int kAeMinWarmupMs = 600;
constexpr double kAeMinUsableMean = 10.0;

bool isSupportedPixelFormat(const libcamera::PixelFormat& format) {
  return format == libcamera::formats::YUYV || format == libcamera::formats::MJPEG ||
         format == libcamera::formats::R8;
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

// Picks a pixel format for the given capture request out of what the camera
// actually offers, in preference order, then validates the configuration.
bool negotiate(libcamera::CameraConfiguration& config, CameraCaptureFormat requested_format,
               const std::string& camera_label) {
  libcamera::StreamConfiguration& stream_config = config.at(0);
  const auto& formats = stream_config.formats();

  std::vector<libcamera::PixelFormat> preference;
  if (requested_format == CameraCaptureFormat::V4L2Grey) {
    // Prefer a true grey stream, but some laptop IR sensors (e.g. Windows
    // Hello cameras) only expose the IR stream as YUYV/MJPEG; decode those
    // instead of failing outright.
    preference = {libcamera::formats::R8, libcamera::formats::YUYV, libcamera::formats::MJPEG};
  } else {
    // Prefer MJPEG for the colour stream: on USB 2.0 cameras an uncompressed
    // 720p YUYV stream plus a concurrently-open IR stream exceeds the isochronous
    // bandwidth budget and the RGB frames arrive truncated (bytesused << expected).
    preference = {libcamera::formats::MJPEG, libcamera::formats::YUYV, libcamera::formats::R8};
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

  const auto validation = config.validate();
  if (validation == libcamera::CameraConfiguration::Invalid) {
    spdlog::error("FaceAuth: Invalid camera configuration for '{}'", camera_label);
    return false;
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

  return true;
}

// Converts one captured plane's bytes into RGB according to the negotiated
// pixel format.
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
  return false;
}

class LibcameraCaptureSession : public ICameraCaptureSession {
 public:
  static std::unique_ptr<LibcameraCaptureSession> open(
      std::shared_ptr<libcamera::CameraManager> manager, std::shared_ptr<libcamera::Camera> camera,
      CameraCaptureFormat requested_format, std::string camera_label,
      std::optional<CaptureProfile> profile) {
    auto session = std::unique_ptr<LibcameraCaptureSession>(
        new LibcameraCaptureSession(std::move(manager), std::move(camera), requested_format,
                                    std::move(camera_label), std::move(profile)));
    if (!session->setup()) {
      return nullptr;
    }
    return session;
  }

  ~LibcameraCaptureSession() override { close(); }

  bool isOpen() const override { return started_; }
  bool isGrey() const override { return is_grey_; }

  ImageRGB capture() override {
    if (!isOpen()) {
      return {};
    }

    const bool has_timeout = profile_.capture_timeout_ms > 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(0, profile_.capture_timeout_ms));

    // Drop any frames that completed before this call so a long-idle session
    // never returns a stale frame.
    drainPending();

    // Colour sessions warm up once per session (auto-exposure settles and
    // stays settled while streaming); grey/IR sessions warm up on every
    // capture to let the emitter settle. Warm-up is time-based with a frame
    // floor so 10 fps and 30 fps cameras behave the same.
    if (is_grey_ || !warmed_up_) {
      const auto warm_start = std::chrono::steady_clock::now();
      const auto warm_until =
          warm_start + std::chrono::milliseconds(std::max(0, profile_.max_warmup_ms));
      // Colour: stop as soon as auto-exposure has converged. Grey/IR: the
      // emitter strobe makes brightness useless as a signal, so run to the cap.
      const bool watch_ae = !is_grey_;
      std::deque<double> recent_means;
      std::string ae_trace;  // per-frame means for the debug log
      int discarded = 0;
      bool converged = false;
      for (;;) {
        libcamera::Request* request = waitForRequest(deadline, has_timeout);
        if (!request) {
          return {};
        }
        if (watch_ae) {
          ImageRGB probe;
          if (extractFrame(request, probe)) {
            recent_means.push_back(meanLuma(probe));
            if (ae_trace.size() < 120) {
              ae_trace += fmt::format("{}{:.0f}", ae_trace.empty() ? "" : ",", recent_means.back());
            }
            if (static_cast<int>(recent_means.size()) > kAeWindow) {
              recent_means.pop_front();
            }
          }
        }
        requeue(request);
        ++discarded;

        if (watch_ae && static_cast<int>(recent_means.size()) == kAeWindow) {
          const auto [lo, hi] = std::minmax_element(recent_means.begin(), recent_means.end());
          const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - warm_start)
                                   .count();
          converged = (*hi - *lo) <= kAeSettleTolerance && elapsed >= kAeMinWarmupMs &&
                      *lo >= kAeMinUsableMean;
        }
        const bool time_up = std::chrono::steady_clock::now() >= warm_until;
        if (discarded >= profile_.min_warmup_frames && (converged || time_up)) {
          break;
        }
      }
      const auto warm_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - warm_start)
                               .count();
      spdlog::debug("FaceAuth: Camera '{}' warmed up in {} ms / {} frames ({}{})", camera_label_,
                    warm_ms, discarded,
                    watch_ae ? (converged ? "AE converged" : "AE cap reached") : "IR settle",
                    watch_ae ? fmt::format(", means=[{}]", ae_trace) : "");
    }
    warmed_up_ = true;

    // Strobed IR emitters: any single frame may land in a dark phase, so
    // sample a few and keep the brightest.
    const int samples = std::max(1, profile_.brightest_of);
    ImageRGB best;
    double best_mean = -1.0;
    for (int i = 0; i < samples; ++i) {
      libcamera::Request* request = waitForRequest(deadline, has_timeout);
      if (!request) {
        break;
      }
      ImageRGB image;
      const bool ok = extractFrame(request, image);
      requeue(request);
      if (!ok) {
        spdlog::error("FaceAuth: Failed to convert frame from '{}'", camera_label_);
        continue;
      }
      const double mean = samples > 1 ? meanLuma(image) : 0.0;
      if (mean > best_mean) {
        best_mean = mean;
        best = std::move(image);
      }
    }
    if (best.empty()) {
      return {};
    }
    if (samples > 1) {
      spdlog::debug("FaceAuth: Grey capture from '{}' kept brightest of {} frames (mean={:.1f})",
                    camera_label_, samples, best_mean);
    }
    return best;
  }

 private:
  LibcameraCaptureSession(std::shared_ptr<libcamera::CameraManager> manager,
                          std::shared_ptr<libcamera::Camera> camera,
                          CameraCaptureFormat requested_format, std::string camera_label,
                          std::optional<CaptureProfile> profile)
      : manager_(std::move(manager)),
        camera_(std::move(camera)),
        camera_label_(std::move(camera_label)),
        is_grey_(requested_format == CameraCaptureFormat::V4L2Grey),
        requested_profile_(std::move(profile)) {}

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
    acquired_ = true;

    if (camera_->configure(config_.get()) != 0) {
      spdlog::error("FaceAuth: Failed to configure camera '{}'", camera_label_);
      return false;
    }

    libcamera::StreamConfiguration& stream_config = config_->at(0);
    pixel_format_ = stream_config.pixelFormat;
    // An IR sensor chosen as the *main* camera is requested as colour but
    // negotiates R8; treat it as grey so strobe handling/warm-up apply.
    if (pixel_format_ == libcamera::formats::R8) {
      is_grey_ = true;
    }
    profile_ = requested_profile_.value_or(is_grey_ ? kIrCaptureProfile : kColourCaptureProfile);
    spdlog::debug(
        "FaceAuth: Camera '{}' capture profile: {} (warm-up ≤{} ms/≥{} frames, brightest of {})",
        camera_label_, is_grey_ ? "IR/grey" : "colour", profile_.max_warmup_ms,
        profile_.min_warmup_frames, profile_.brightest_of);
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

    if (camera_->start() < 0) {
      spdlog::error("FaceAuth: Failed to start camera '{}'", camera_label_);
      camera_->requestCompleted.disconnect(&connection_token_);
      return false;
    }
    started_ = true;

    for (auto& request : requests_) {
      if (camera_->queueRequest(request.get()) < 0) {
        spdlog::error("FaceAuth: Failed to queue initial request for '{}'", camera_label_);
        started_ = false;
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

  // Blocks until a completed request is available or the deadline passes.
  libcamera::Request* waitForRequest(std::chrono::steady_clock::time_point deadline,
                                     bool has_timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool have_request;
    if (has_timeout) {
      have_request = ready_.wait_until(lock, deadline, [this] { return !completed_.empty(); });
    } else {
      ready_.wait(lock, [this] { return !completed_.empty(); });
      have_request = true;
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

  void drainPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!completed_.empty()) {
      libcamera::Request* request = completed_.front();
      completed_.pop_front();
      request->reuse(libcamera::Request::ReuseBuffers);
      camera_->queueRequest(request);
    }
  }

  void requeue(libcamera::Request* request) {
    request->reuse(libcamera::Request::ReuseBuffers);
    camera_->queueRequest(request);
  }

  bool extractFrame(libcamera::Request* request, ImageRGB& out) {
    libcamera::FrameBuffer* buffer = request->findBuffer(stream_);
    if (!buffer) {
      return false;
    }
    const auto it = mappings_.find(buffer);
    if (it == mappings_.end()) {
      return false;
    }
    const auto metadata = buffer->metadata().planes();
    if (metadata.empty()) {
      return false;
    }
    const size_t bytes_used = metadata[0].bytesused;
    const uint8_t* data = static_cast<const uint8_t*>(it->second.base) + it->second.plane_offset;
    return convertFrame(pixel_format_, data, bytes_used, width_, height_, stride_, out);
  }

  void close() {
    if (started_) {
      camera_->stop();
      started_ = false;
    }
    camera_->requestCompleted.disconnect(&connection_token_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      completed_.clear();
    }
    for (auto& [buffer, mapping] : mappings_) {
      munmap(mapping.base, mapping.size);
    }
    mappings_.clear();
    requests_.clear();
    allocator_.reset();
    if (acquired_) {
      camera_->release();
      acquired_ = false;
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
  std::optional<CaptureProfile> requested_profile_;
  CaptureProfile profile_ = kColourCaptureProfile;
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

  bool acquired_ = false;
  bool started_ = false;
};

}  // namespace

const char* sensorTag(SensorKind kind) {
  switch (kind) {
    case SensorKind::Rgb:
      return "rgb";
    case SensorKind::Ir:
      return "ir";
    default:
      return "";
  }
}

SensorKind sensorKindOfEnrolment(const std::string& path) {
  const auto slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  if (name.find(".ir.") != std::string::npos) return SensorKind::Ir;
  if (name.find(".rgb.") != std::string::npos) return SensorKind::Rgb;
  return SensorKind::Unknown;
}

std::string tagEnrolmentPath(const std::string& path, SensorKind kind) {
  if (kind == SensorKind::Unknown || sensorKindOfEnrolment(path) != SensorKind::Unknown) {
    return path;
  }
  const auto slash = path.find_last_of('/');
  const auto dot = path.find_last_of('.');
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    return path + "." + sensorTag(kind);
  }
  return path.substr(0, dot) + "." + sensorTag(kind) + path.substr(dot);
}

bool checkCameraAvailability(const std::optional<std::string>& linux_video_device_path) {
  auto session = openCameraSession(linux_video_device_path);
  return session && session->isOpen();
}

std::unique_ptr<ICameraCaptureSession> openCameraSession(
    const std::optional<std::string>& linux_video_device_path, CameraCaptureFormat format,
    std::optional<CaptureProfile> profile) {
  auto manager = cameraManager();
  if (!manager) {
    return nullptr;
  }

  auto camera = findCamera(*manager, linux_video_device_path);
  if (!camera) {
    return nullptr;
  }

  return LibcameraCaptureSession::open(manager, camera, format,
                                       device_label(linux_video_device_path), std::move(profile));
}

ImageRGB captureImage(const std::optional<std::string>& linux_video_device_path,
                      CameraCaptureFormat format) {
  auto session = openCameraSession(linux_video_device_path, format);
  if (!session) {
    return {};
  }

  return session->capture();
}

ImageRGB captureImageByIRCamera(const std::string& device_path, const CaptureProfile& profile) {
  if (device_path.empty()) {
    spdlog::error("FaceAuth: IR camera capture requires a /dev/video* path");
    return {};
  }

  auto session = openCameraSession(device_path, CameraCaptureFormat::V4L2Grey, profile);
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
