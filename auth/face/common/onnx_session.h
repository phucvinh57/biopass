#pragma once

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <vector>

namespace biopass {

// Owns the ONNX Runtime session plumbing (env, session, allocator, I/O name
// tables) shared by every inference engine in this module (detection,
// recognition, anti-spoofing). Engines compose this and only implement their
// own pre/postprocessing.
class OnnxSession {
 public:
  OnnxSession(const std::string& model_path, const char* log_name);

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
