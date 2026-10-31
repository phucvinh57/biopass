import { useEffect, useState } from "react";
import { cmd } from "@/commands";
import type { Model } from "@/types/config";

/** All registered models; reloads when the default models are updated. */
export function useModels(): Model[] {
  const [models, setModels] = useState<Model[]>([]);

  useEffect(() => {
    const fetchModels = async () => {
      try {
        setModels(await cmd.models.list());
      } catch (err) {
        console.error("Failed to fetch models:", err);
      }
    };

    fetchModels();
    const unlisten = cmd.models.onDefaultsUpdated(fetchModels);
    return () => {
      unlisten.then((fn) => fn());
    };
  }, []);

  return models;
}
