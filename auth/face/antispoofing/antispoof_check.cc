#include "antispoof_check.h"

#include <spdlog/spdlog.h>

#include <future>
#include <memory>
#include <vector>

#include "debug_image_io.h"
#include "face_as.h"
#include "ir_camera_as.h"
#include "onnx_session.h"

namespace biopass {

namespace {

struct AntiSpoofTask {
  std::string name;
  std::future<bool> future;
};

AntiSpoofTask make_task(const std::string& name, std::future<bool> future) {
  AntiSpoofTask task;
  task.name = name;
  task.future = std::move(future);
  return task;
}

// nullptr, logged, when the model is not in the registry or its architecture
// is unknown. Throws if building the engine fails.
std::unique_ptr<FaceAntiSpoofing> loadAntiSpoofModel(const AntiSpoofingModelConfig& modelCfg,
                                                     const std::string& username,
                                                     const ModelRegistry& model_registry) {
  const std::string& modelId = modelCfg.model_id;
  const auto model = model_registry.resolveModel(modelId);
  if (!model) {
    spdlog::error("FaceAuth: Anti-spoofing model '{}' not found in the registry", modelId);
    return nullptr;
  }

  const auto architecture = parseAntiSpoofArchitecture(model->architecture);
  if (!architecture) {
    spdlog::error("FaceAuth: Anti-spoofing model '{}' has unknown architecture '{}'", modelId,
                  model->architecture);
    return nullptr;
  }

  // The path comes from the user's registry: load it as the user.
  return makeEngineAsUser<FaceAntiSpoofing>(username, model->path, *architecture, 128,
                                            modelCfg.threshold);
}

bool checkAntiSpoofByAIModel(const AntiSpoofingModelConfig& modelCfg, const std::string& username,
                             const ImageRGB& frame, const Box& face_box, const AuthConfig& authCfg,
                             const ModelRegistry& model_registry,
                             SessionEngine<FaceAntiSpoofing>& ai_model) {
  try {
    FaceAntiSpoofing* face_as =
        ai_model.get([&] { return loadAntiSpoofModel(modelCfg, username, model_registry); });
    if (!face_as) {
      return false;
    }
    const SpoofResult result = face_as->inference(frame, face_box);
    if (result.spoof) {
      spdlog::warn("FaceAuth: AI anti-spoofing detected spoof, score: {}", result.score);
      if (authCfg.debug) {
        saveFailedFace(username, frame.crop(face_box.x1, face_box.y1, face_box.x2, face_box.y2),
                       "spoof");
      }
      return false;
    }

    spdlog::debug("FaceAuth: AI anti-spoofing check passed");
    return true;
  } catch (const std::exception& e) {
    spdlog::error("FaceAuth: AI anti-spoofing check failed: {}", e.what());
    return false;
  }
}

}  // namespace

bool checkAntiSpoof(const FaceMethodConfig& face_config, const std::string& username,
                    const ImageRGB& frame, const Box& face_box, const AuthConfig& config,
                    const ModelRegistry& model_registry, SessionEngine<FaceAntiSpoofing>& ai_model,
                    FaceDetection* shared_detector, ICameraCaptureSession* ir_camera_session,
                    const std::atomic<bool>* cancel) {
  const auto ai = aiAntiSpoofingModel(face_config.anti_spoofing);
  const auto ir = irCameraConfig(face_config.anti_spoofing);

  if (!ai && !ir) {
    spdlog::debug("FaceAuth: Anti-spoofing methods are disabled, skipping checks");
    return true;
  }

  spdlog::debug("FaceAuth: Anti-spoofing started (ai_enabled={}, ir_enabled={}, ir_camera='{}')",
                ai.has_value(), ir.has_value(), face_config.anti_spoofing.ir_camera.value_or(""));

  std::vector<AntiSpoofTask> tasks;

  if (ai) {
    const AntiSpoofingModelConfig model_config = *ai;
    const auto username_copy = username;
    const auto config_copy = config;
    const auto* model_registry_ptr = &model_registry;
    // Only this task touches the engine, and every task is joined below
    // before returning, so the next attempt's task sees what this one loaded.
    auto* ai_model_ptr = &ai_model;
    auto shared_frame = std::make_shared<const ImageRGB>(frame);
    const Box face_box_copy = face_box;
    tasks.push_back(make_task(
        "AI",
        std::async(std::launch::async, [model_config, username_copy, shared_frame, face_box_copy,
                                        config_copy, model_registry_ptr, ai_model_ptr]() {
          return checkAntiSpoofByAIModel(model_config, username_copy, *shared_frame, face_box_copy,
                                         config_copy, *model_registry_ptr, *ai_model_ptr);
        })));
  }

  if (ir) {
    const IrCameraConfig ir_config = *ir;
    const auto username_copy = username;
    const auto debug_enabled = config.debug;
    auto* ir_camera_session_ptr = ir_camera_session;
    tasks.push_back(make_task(
        "IR", std::async(std::launch::async, [ir_config, shared_detector, username_copy,
                                              debug_enabled, ir_camera_session_ptr, cancel]() {
          return checkAntispoofByIRCamera(ir_config, shared_detector, username_copy, debug_enabled,
                                          ir_camera_session_ptr, cancel);
        })));
  }

  bool all_passed = true;
  for (auto& task : tasks) {
    bool ok = false;
    try {
      ok = task.future.get();
    } catch (const std::exception& e) {
      spdlog::error("FaceAuth: {} anti-spoofing task failed: {}", task.name, e.what());
      ok = false;
    }
    if (ok) {
      spdlog::debug("FaceAuth: {} anti-spoofing method passed", task.name);
    } else {
      spdlog::debug("FaceAuth: {} anti-spoofing method failed", task.name);
      all_passed = false;
    }
  }

  if (!all_passed) {
    spdlog::error("FaceAuth: Anti-spoofing failed (one or more enabled methods failed)");
  }
  return all_passed;
}

}  // namespace biopass
