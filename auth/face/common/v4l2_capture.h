#pragma once

// Direct V4L2 capture for devices libcamera does not list (custom DKMS
// drivers, v4l2loopback). Used only for an explicit /dev/video* path.

#include <libcamera/pixel_format.h>
#include <sys/types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "camera_capture.h"

namespace biopass {

// The libcamera format a V4L2 fourcc decodes as; nullopt if unsupported.
std::optional<libcamera::PixelFormat> pixelFormatFromV4l2(uint32_t fourcc);

// The most preferred fourcc (pixelFormatPreference order) in `available`.
std::optional<uint32_t> pickV4l2Format(const std::vector<uint32_t>& available,
                                       CameraCaptureFormat format);

// True for a character device with the V4L2 major (81). The fallback opens
// only these: other device nodes can have side effects on open.
bool isVideoCharDevice(mode_t mode, dev_t rdev);

// The driver's bytesperline, or the packed row size when it reports 0.
uint32_t v4l2BytesPerLine(uint32_t fourcc, uint32_t width, uint32_t reported);

class V4l2CaptureSession : public ICameraCaptureSession {
 public:
  static std::unique_ptr<V4l2CaptureSession> open(const std::string& device_path,
                                                  CameraCaptureFormat requested_format,
                                                  int warmup_frames, int capture_timeout_ms);

  ~V4l2CaptureSession() override { close(); }

  bool isOpen() const override { return fd_ >= 0 && streaming_; }
  ImageRGB capture() override;

  // Safe to call more than once.
  void close();

 private:
  struct Buffer {
    void* data = nullptr;
    size_t length = 0;
  };

  struct Dequeued {
    uint32_t index = 0;
    size_t bytes_used = 0;
    bool error = false;
  };

  V4l2CaptureSession(std::string device_path, CameraCaptureFormat requested_format,
                     int warmup_frames, int capture_timeout_ms);

  bool setup(CameraCaptureFormat requested_format);
  bool startStreaming();
  bool waitForBuffer(std::chrono::steady_clock::time_point deadline, bool has_timeout,
                     Dequeued& out);
  bool drainPending();
  bool requeue(uint32_t index);
  bool frameUsable(const Dequeued& frame) const;
  bool extractFrame(const Dequeued& frame, ImageRGB& out);

  std::string device_path_;
  bool is_grey_ = false;
  int warmup_frames_ = 0;
  int capture_timeout_ms_ = 0;
  bool warmed_up_ = false;

  int fd_ = -1;
  bool buffers_requested_ = false;
  bool streaming_ = false;
  std::vector<Buffer> buffers_;

  uint32_t fourcc_ = 0;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
  bool warned_frame_size_ = false;
};

// For camera_capture_test: the formats a V4L2 device reports.
std::vector<CameraFormatDesc> listV4l2Formats(const std::string& device_path);

}  // namespace biopass
