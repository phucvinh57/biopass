import { commands } from "@/bindings";

function listImages() {
  return commands.listFaces();
}

async function startPreview(camera?: string | null): Promise<void> {
  await commands.startFacePreview(camera ?? null);
}

async function stopPreview(): Promise<void> {
  await commands.stopFacePreview();
}

function captureInSession() {
  return commands.captureFaceInSession();
}

async function deleteImage(path: string): Promise<void> {
  await commands.deleteFace(path);
}

function listVideoDevices() {
  return commands.listVideoDevices();
}
export const face = {
  listImages,
  startPreview,
  stopPreview,
  captureInSession,
  deleteImage,
  listVideoDevices,
};
