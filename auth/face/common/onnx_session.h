#pragma once

#include <onnxruntime_cxx_api.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace biopass {

// Contents of an .onnx file. Sessions are built from memory so the file can
// be read safely first: in the root helper, models come from paths the user
// controls (see readUserModel).
struct ModelBytes {
  std::string data;
};

// Larger than any face model; only stops absurd files.
inline constexpr size_t kMaxModelBytes = 512u << 20;

// Reads a regular model file the caller may open directly (see
// core/user_file.h). Throws std::runtime_error on failure.
ModelBytes loadModelFile(const std::string& path);

// Reads a model path taken from the user's registry as that user, so root
// only loads files the user could read. nullopt on failure.
std::optional<ModelBytes> readUserModel(const std::string& username, const std::string& path);

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

}  // namespace biopass
