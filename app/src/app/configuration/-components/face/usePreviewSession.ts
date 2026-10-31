import { useEffect, useRef, useState } from "react";
import { cmd } from "@/commands";

/**
 * Runs the face preview on `camera` while the session is active.
 *
 * Each (active, camera) state does exactly one start, and leaving it does
 * exactly one stop. All calls go through one promise chain, so they reach
 * the backend in order: a camera switch is stop then start, and a start
 * still in flight on unmount is followed by its stop. A start whose state
 * was left before its turn is skipped, so nothing starts after unmount.
 */
export function usePreviewSession(
  camera: string | null | undefined,
  onStartError: (err: unknown) => void,
) {
  const [active, setActive] = useState(false);
  const chain = useRef<Promise<void>>(Promise.resolve());
  const onStartErrorRef = useRef(onStartError);
  onStartErrorRef.current = onStartError;

  useEffect(() => {
    if (!active) return;

    function enqueue(op: () => Promise<void>) {
      const next = chain.current.then(op);
      chain.current = next.catch(() => {});
      return next;
    }

    let current = true;
    enqueue(() =>
      current ? cmd.face.startPreview(camera) : Promise.resolve(),
    ).catch((err) => {
      if (!current) return;
      setActive(false);
      onStartErrorRef.current(err);
    });

    return () => {
      current = false;
      enqueue(() => cmd.face.stopPreview()).catch((err) =>
        console.error("stopPreview failed:", err),
      );
    };
  }, [active, camera]);

  return {
    active,
    start: () => setActive(true),
    stop: () => setActive(false),
  };
}
