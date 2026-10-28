import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import type { AntiSpoofArchitecture, Model, ModelTask } from "@/types/config";
import { invokeCommand } from "./core";

function list(task?: ModelTask) {
  return invokeCommand<Model[]>("list_models", {
    task: task ?? null,
  });
}

function addFromUrl(
  name: string,
  task: ModelTask,
  architecture: AntiSpoofArchitecture | null,
  url: string,
) {
  return invokeCommand<Model>("add_model_from_url", {
    name,
    task,
    architecture,
    url,
  });
}

function addFromFile(
  name: string,
  task: ModelTask,
  architecture: AntiSpoofArchitecture | null,
  srcPath: string,
) {
  return invokeCommand<Model>("add_model_from_file", {
    name,
    task,
    architecture,
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

// Downloads any missing built-in model. Returns the names it downloaded.
function ensureDefaults() {
  return invokeCommand<string[]>("ensure_default_models");
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
