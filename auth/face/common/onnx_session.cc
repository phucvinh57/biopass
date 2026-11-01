#include "onnx_session.h"

#include <optional>

#include "user_file.h"

namespace biopass {

ModelBytes loadModelFile(const std::string& path) {
  std::optional<std::string> data = readRegularFile(path, kMaxModelBytes);
  if (!data) {
    throw std::runtime_error("cannot read model file: " + path);
  }
  return ModelBytes{std::move(*data)};
}

OnnxSession::OnnxSession(const ModelBytes& model, const char* log_name)
    : env_(ORT_LOGGING_LEVEL_WARNING, log_name) {
  Ort::SessionOptions opts;
  opts.SetIntraOpNumThreads(1);
  opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

  session_ = std::make_unique<Ort::Session>(env_, model.data.data(), model.data.size(), opts);

  for (size_t i = 0; i < session_->GetInputCount(); i++) {
    auto name = session_->GetInputNameAllocated(i, allocator_);
    input_names_str_.push_back(name.get());
  }
  for (size_t i = 0; i < session_->GetOutputCount(); i++) {
    auto name = session_->GetOutputNameAllocated(i, allocator_);
    output_names_str_.push_back(name.get());
  }
  for (auto& s : input_names_str_) input_names_cstr_.push_back(s.c_str());
  for (auto& s : output_names_str_) output_names_cstr_.push_back(s.c_str());
}

std::vector<Ort::Value> OnnxSession::run(std::vector<float>& input,
                                         const std::vector<int64_t>& shape) {
  auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, input.data(), input.size(),
                                                            shape.data(), shape.size());

  return session_->Run(Ort::RunOptions{nullptr}, input_names_cstr_.data(), &input_tensor, 1,
                       output_names_cstr_.data(), output_names_cstr_.size());
}

std::vector<int64_t> OnnxSession::inputShape() const {
  return session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
}

}  // namespace biopass
