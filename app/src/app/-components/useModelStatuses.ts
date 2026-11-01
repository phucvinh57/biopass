import { useCallback, useEffect, useState } from "react";
import { toast } from "sonner";
import { cmd } from "@/commands";
import { useModelFileStatus } from "@/hooks/useModelFileStatus";
import { referencedModelIds } from "@/lib/models";
import type { Model } from "@/types/config";
import type { ModelStatusType } from "./ModelStatus";

/**
 * The registered models and each one's status. Loads on mount and again when
 * the default models are updated. `setModels` applies a local add, rename or
 * delete without reloading.
 */
export function useModelStatuses() {
  const [models, setModels] = useState<Model[]>([]);
  const [loading, setLoading] = useState(true);
  // null until the saved config is read; keeps every model "checking" (and
  // undeletable) if that fails.
  const [inUseIds, setInUseIds] = useState<Set<string> | null>(null);
  const fileStatus = useModelFileStatus(models);

  const loadInUseIds = useCallback(async () => {
    try {
      const config = await cmd.config.load();
      setInUseIds(
        new Set(referencedModelIds(config, { includeDisabled: true })),
      );
    } catch (err) {
      console.error("Failed to check model usage:", err);
    }
  }, []);

  function statusOf(model: Model): ModelStatusType {
    const exists = fileStatus[model.id];
    if (exists === undefined || inUseIds === null) return "checking";
    if (!exists) return "missing";
    return inUseIds.has(model.id) ? "inuse" : "available";
  }

  const loadModels = useCallback(async () => {
    try {
      setLoading(true);
      const loadedModels = await cmd.models.list();
      setModels(loadedModels);
      await loadInUseIds();
    } catch (err) {
      console.error("Failed to load models:", err);
      toast.error("Failed to load models");
    } finally {
      setLoading(false);
    }
  }, [loadInUseIds]);

  useEffect(() => {
    loadModels();
    const unlisten = cmd.models.onDefaultsUpdated(loadModels);
    return () => {
      unlisten.then((fn) => fn());
    };
  }, [loadModels]);

  return { models, setModels, loading, statusOf };
}
