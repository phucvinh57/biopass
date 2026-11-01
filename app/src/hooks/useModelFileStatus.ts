import { useEffect, useState } from "react";
import { cmd } from "@/commands";
import type { Model } from "@/types/config";

/**
 * Whether each model's file exists, by model id. An id is absent until its
 * first check finishes; a failed check counts as missing. Rechecks whenever
 * `models` changes identity, so pass a memoized array.
 */
export function useModelFileStatus(
  models: Model[],
): Record<string, boolean | undefined> {
  const [status, setStatus] = useState<Record<string, boolean>>({});

  useEffect(() => {
    let cancelled = false;
    const check = async () => {
      const next: Record<string, boolean> = {};
      await Promise.all(
        models.map(async (m) => {
          try {
            next[m.id] = await cmd.file.exists(m.path);
          } catch (err) {
            console.error(`Status check failed for ${m.path}:`, err);
            next[m.id] = false;
          }
        }),
      );
      if (!cancelled) setStatus(next);
    };

    check();
    return () => {
      cancelled = true;
    };
  }, [models]);

  return status;
}
