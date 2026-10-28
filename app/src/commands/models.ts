import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import type { Model, ModelType } from "@/types/config";
import { invokeCommand } from "./core";

function list(modelType?: ModelType) {
  return invokeCommand<Model[]>("list_models", {
    modelType: modelType ?? null,
  });
}

function addFromUrl(name: string, modelType: ModelType, url: string) {
  return invokeCommand<Model>("add_model_from_url", {
    name,
    modelType,
    url,
  });
}

function addFromFile(name: string, modelType: ModelType, srcPath: string) {
  return invokeCommand<Model>("add_model_from_file", {
    name,
    modelType,
    srcPath,
  });
}

function remove(id: string) {
  return invokeCommand<void>("delete_model", { id });
}

function rename(id: string, name: string) {
  return invokeCommand<Model>("rename_model", { id, name });
}

export interface ModelDownloadProgress {
  id: string;
  downloaded: number;
  total: number | null;
}

function onDownloadProgress(
  cb: (progress: ModelDownloadProgress) => void,
): Promise<UnlistenFn> {
  return listen<ModelDownloadProgress>("model-download-progress", (event) => {
    cb(event.payload);
  });
}

export interface DefaultModelsReport {
  downloaded: string[];
  failed: string[];
}

// Downloads any missing built-in model into the user's data dir and registers
// it. Safe to call repeatedly; it's a no-op once all files are present.
function ensureDefaults() {
  return invokeCommand<DefaultModelsReport>("ensure_default_models");
}

function onDefaultsUpdated(cb: () => void): Promise<UnlistenFn> {
  return listen("default-models-updated", () => cb());
}

export const models = {
  list,
  addFromUrl,
  addFromFile,
  remove,
  rename,
  onDownloadProgress,
  ensureDefaults,
  onDefaultsUpdated,
};
