#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera_capture.h"
#include "image_utils.h"

namespace {

bool is_unsigned_number(const std::string& value) {
  return !value.empty() && std::all_of(value.begin(), value.end(),
                                       [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

void print_devices(const std::vector<biopass::CameraDeviceInfo>& devices) {
  if (devices.empty()) {
    std::cout << "No capture devices found.\n";
    return;
  }

  std::cout << "Available devices:\n";
  for (size_t index = 0; index < devices.size(); ++index) {
    const auto& device = devices[index];
    std::cout << "  [" << index << "] " << device.model << '\n';
    std::cout << "      id: " << device.id << '\n';
    for (const auto& path : device.video_paths) {
      std::cout << "      linux path: " << path << '\n';
    }
  }
}

void print_formats(const std::string& device_path) {
  const auto formats = biopass::listCameraFormats(device_path);
  if (formats.empty()) {
    std::cout << "No formats reported for device " << device_path << ".\n";
    return;
  }

  std::cout << "Available formats for " << device_path << ":\n";
  for (const auto& format : formats) {
    std::cout << "  " << format.pixel_format << " supported=" << (format.supported ? "yes" : "no")
              << " sizes=";
    for (size_t i = 0; i < format.sizes.size(); ++i) {
      if (i > 0) {
        std::cout << ", ";
      }
      std::cout << format.sizes[i].first << 'x' << format.sizes[i].second;
    }
    std::cout << '\n';
  }
}

// Resolves a device selector to a /dev/video* path. Accepts a Linux path
// directly, a bare index into listCameraDevices(), or a model substring.
std::string resolve_device_selector(const std::string& selector) {
  if (selector.rfind("/dev/video", 0) == 0) {
    return selector;
  }

  const auto devices = biopass::listCameraDevices();
  if (devices.empty()) {
    throw std::runtime_error("No capture devices found.");
  }

  if (is_unsigned_number(selector)) {
    const auto parsed = static_cast<size_t>(std::stoul(selector));
    if (parsed >= devices.size()) {
      throw std::runtime_error("Device index out of range: " + selector);
    }
    if (devices[parsed].video_paths.empty()) {
      throw std::runtime_error("Device [" + selector + "] has no /dev/video* path");
    }
    return devices[parsed].video_paths.front();
  }

  std::vector<std::string> matches;
  for (const auto& device : devices) {
    if (device.id.find(selector) != std::string::npos ||
        device.model.find(selector) != std::string::npos) {
      if (!device.video_paths.empty()) {
        matches.push_back(device.video_paths.front());
      }
    }
  }

  if (matches.size() == 1) {
    return matches.front();
  }
  if (matches.size() > 1) {
    throw std::runtime_error("Device selector is ambiguous: " + selector);
  }
  throw std::runtime_error("No device matches selector: " + selector);
}

}  // namespace

int main(int argc, char** argv) {
  namespace fs = std::filesystem;

  spdlog::set_default_logger(spdlog::stderr_color_mt("camera_capture_test"));
  spdlog::set_level(spdlog::level::debug);

  CLI::App app("Capture a single image frame from a camera via libcamera.");

  std::string device_selector;
  std::string output_path = "capture.jpg";
  bool grey = false;
  int warmup_frames = 5;
  int timeout_ms = 10000;
  // Deprecated aliases retained so existing field scripts keep working.
  int attempts = 0;
  int poll_interval_ms = 10;
  bool list_devices = false;
  bool list_formats = false;

  app.add_option("device", device_selector,
                 "Device selector. Accepts a Linux path like /dev/video0, a device index, or a "
                 "model substring.");
  app.add_flag("--list-devices", list_devices, "List available capture devices and exit.");
  app.add_flag("--list-formats", list_formats,
               "List available formats for the selected device and exit.");
  app.add_option("-o,--output", output_path, "Output image path.")->default_val(output_path);
  app.add_flag("--grey", grey, "Capture from the camera's GREY/R8 stream (IR cameras).");
  app.add_option("--warmup-frames", warmup_frames, "Frames to discard before capture.")
      ->default_val(warmup_frames);
  app.add_option("--timeout-ms", timeout_ms, "Capture deadline in milliseconds.")
      ->default_val(timeout_ms);
  app.add_option("--attempts", attempts,
                 "Deprecated: computes --timeout-ms as attempts * --poll-interval-ms.");
  app.add_option("--poll-interval-ms", poll_interval_ms,
                 "Deprecated: used only together with --attempts.")
      ->default_val(poll_interval_ms);

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return app.exit(e);
  }

  if (attempts > 0) {
    timeout_ms = attempts * poll_interval_ms;
  }

  if (!list_devices && device_selector.empty()) {
    std::cerr << "A device selector is required unless --list-devices is used.\n";
    return 1;
  }

  try {
    if (list_devices) {
      print_devices(biopass::listCameraDevices());
      if (device_selector.empty()) {
        return 0;
      }
    }

    const std::string device_path = resolve_device_selector(device_selector);

    if (list_formats) {
      print_formats(device_path);
      return 0;
    }

    const auto format =
        grey ? biopass::CameraCaptureFormat::V4L2Grey : biopass::CameraCaptureFormat::Default;
    std::cout << "Capturing from " << device_path << " (grey=" << (grey ? "yes" : "no") << ")\n";
    std::cout.flush();

    auto session = biopass::openCameraSession(device_path, format, warmup_frames, timeout_ms);
    if (!session) {
      std::cerr << "Failed to open camera session for " << device_path << '\n';
      return 1;
    }

    const ImageRGB image = session->capture();
    if (image.empty()) {
      std::cerr << "Capture returned an empty image.\n";
      return 1;
    }

    const fs::path absolute_output_path = fs::absolute(fs::path(output_path));
    const fs::path parent_dir = absolute_output_path.parent_path();
    if (!parent_dir.empty()) {
      fs::create_directories(parent_dir);
    }

    if (!saveImage(absolute_output_path.string(), image)) {
      std::cerr << "Failed to save image to " << absolute_output_path << '\n';
      return 1;
    }

    std::cout << "Captured " << image.width << 'x' << image.height << " from " << device_path
              << '\n';
    std::cout << "Saved image to " << absolute_output_path << '\n';
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }

  return 0;
}
