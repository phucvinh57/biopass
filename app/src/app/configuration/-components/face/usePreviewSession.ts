import { useEffect, useRef, useState } from "react";
import { cmd } from "@/commands";

// The backend has one preview slot for the whole app, so its calls share one
// queue across mounts: a stop queued by an unmounted page still runs before
// the next page's start, instead of killing the session that start made.
let chain: Promise<void> = Promise.resolve();

function enqueue(op: () => Promise<void>) {
  const next = chain.then(op);
  chain = next.catch(() => {});
  return next;
}

/**
 * Runs the face preview on `camera` while the session is active.
 *
 * Each (active, camera) state does exactly one start, and leaving it does
 * exactly one stop, in order through the shared queue: a camera switch is
 * stop then start, and a start still in flight on unmount is followed by its
 * stop. A start whose state was left before its turn is skipped, so nothing
 * starts after unmount. `onLeave` runs whenever a state is left (stop, camera
 * switch, failed start, unmount).
 */
export function usePreviewSession(
  camera: string | null | undefined,
  onStartError: (err: unknown) => void,
  onLeave: () => void,
) {
  const [active, setActive] = useState(false);
  const callbacks = useRef({ onStartError, onLeave });
  callbacks.current = { onStartError, onLeave };

  useEffect(() => {
    if (!active) return;

    let current = true;
    enqueue(() =>
      current ? cmd.face.startPreview(camera) : Promise.resolve(),
    ).catch((err) => {
      if (!current) return;
      setActive(false);
      callbacks.current.onStartError(err);
    });

    return () => {
      current = false;
      callbacks.current.onLeave();
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
