import type { BiopassConfig } from "@/types/config";

/**
 * Model ids the config names, without empties or duplicates.
 *
 * - `includeDisabled: true` — every id the config names, even under a
 *   disabled face method or disabled anti-spoofing. Use it for delete
 *   protection and the "In Use" badge: re-enabling a method must never
 *   resolve to a deleted model. Same rule as `is_model_referenced` in
 *   src-tauri/src/models.rs.
 * - `includeDisabled: false` — only the models authentication would load
 *   with this config. Use it to check model files before saving.
 */
export function referencedModelIds(
  config: BiopassConfig,
  { includeDisabled }: { includeDisabled: boolean },
): string[] {
  const face = config.methods.face;
  const ids: string[] = [];
  if (includeDisabled || face.enable) {
    ids.push(face.detection.model_id, face.recognition.model_id);
    if (includeDisabled || face.anti_spoofing.enable) {
      ids.push(face.anti_spoofing.model.model_id);
    }
  }
  return [...new Set(ids.filter((id) => id !== ""))];
}
