import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { commands } from "@/bindings";
import type {
  AntiSpoofArchitecture,
  Model,
  ModelDownloadProgress,
  ModelTask,
} from "@/types/config";

function list(task?: ModelTask): Promise<Model[]> {
  return commands.listModels(task ?? null);
}

function addFromUrl(
  name: string,
  task: ModelTask,
  architecture: AntiSpoofArchitecture | null,
  url: string,
): Promise<Model> {
  return commands.addModelFromUrl(name, task, architecture, url);
}

function addFromFile(
  name: string,
  task: ModelTask,
  architecture: AntiSpoofArchitecture | null,
  srcPath: string,
): Promise<Model> {
  return commands.addModelFromFile(name, task, architecture, srcPath);
}

async function remove(id: string): Promise<void> {
  await commands.deleteModel(id);
}

function rename(id: string, name: string): Promise<Model> {
  return commands.renameModel(id, name);
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
  return commands.ensureDefaultModels();
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
