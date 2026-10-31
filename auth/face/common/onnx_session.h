#pragma once

#include <onnxruntime_cxx_api.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "fs_user.h"

namespace biopass {

// Contents of an .onnx file, read with readRegularFile() (core/user_file.h)
// so a device or FIFO is never loaded as a model.
struct ModelBytes {
  std::string data;
};

// Larger than any face model; only stops absurd files.
inline constexpr size_t kMaxModelBytes = 512u << 20;

// Reads a regular model file the caller may open directly (see
// core/user_file.h). Throws std::runtime_error on failure.
ModelBytes loadModelFile(const std::string& path);

// ONNX Runtime plumbing (env, session, allocator, I/O names) shared by the
// detection, recognition and anti-spoofing engines.
class OnnxSession {
 public:
  OnnxSession(const ModelBytes& model, const char* log_name);

  std::vector<Ort::Value> run(std::vector<float>& input, const std::vector<int64_t>& shape);

  // Shape of the first model input; dynamic dimensions are -1.
  std::vector<int64_t> inputShape() const;

 private:
  Ort::Env env_;
  std::unique_ptr<Ort::Session> session_;
  Ort::AllocatorWithDefaultOptions allocator_;
  std::vector<std::string> input_names_str_;
  std::vector<std::string> output_names_str_;
  std::vector<const char*> input_names_cstr_;
  std::vector<const char*> output_names_cstr_;
};

// Text up to the first '\n'. ONNX Runtime's load errors can run to many
// lines; logs keep only the first.
inline std::string firstLine(const std::string& text) { return text.substr(0, text.find('\n')); }

// Builds an engine (OnnxSession, or a class taking ModelBytes first) from a
// model path the user controls, such as one from the user's registry in the
// root helper. Reading the model and building the session both run as the
// user (fs_user.h): ONNX Runtime opens an initializer's external data file
// by itself while building, so that path gets the user's permissions too.
// Pool threads started here keep the user's filesystem ids, which only takes
// privileges away. Throws on any failure.
template <typename Engine, typename... Args>
std::unique_ptr<Engine> makeEngineAsUser(const std::string& username, const std::string& path,
                                         Args&&... args) {
  ScopedFsUser as_user(username);
  if (!as_user.ok()) {
    throw std::runtime_error("cannot switch to user " + username + " to load " + path);
  }
  return std::make_unique<Engine>(loadModelFile(path), std::forward<Args>(args)...);
}

}  // namespace biopass
