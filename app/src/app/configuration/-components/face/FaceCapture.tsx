import { convertFileSrc } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { Camera, Circle, Square, Trash2 } from "lucide-react";
import { useEffect, useRef } from "react";
import { useWatch } from "react-hook-form";
import { toast } from "sonner";
import { Button } from "@/components/ui/button";
import type { BiopassConfig } from "@/types/config";
import { useFaceImages } from "./useFaceImages";
import { usePreviewSession } from "./usePreviewSession";

export function FaceCapture() {
  const previewRef = useRef<HTMLImageElement>(null);
  const camera = useWatch<BiopassConfig, "methods.face.camera">({
    name: "methods.face.camera",
  });
  const {
    active: capturing,
    start: startCamera,
    stop: stopCamera,
  } = usePreviewSession(
    camera,
    (err) => {
      toast.error(`Failed to start camera: ${err}`);
      console.error(err);
    },
    // Drop the last frame so a failed start or camera switch doesn't show it.
    () => previewRef.current?.removeAttribute("src"),
  );
  const {
    faceImages,
    capture: capturePhoto,
    remove: deleteFace,
  } = useFaceImages();

  useEffect(() => {
    if (!capturing) return;

    let unlisten: UnlistenFn | undefined;
    let cancelled = false;

    listen<string>("face-preview-frame", (event) => {
      if (previewRef.current) {
        previewRef.current.src = `data:image/jpeg;base64,${event.payload}`;
      }
    }).then((u) => {
      if (cancelled) {
        u();
      } else {
        unlisten = u;
      }
    });

    return () => {
      cancelled = true;
      unlisten?.();
    };
  }, [capturing]);

  return (
    <div className="p-4 rounded-lg bg-muted/50 border border-border/50">
      <h4 className="font-medium mb-3 flex items-center gap-2">
        <Camera className="w-4 h-4" />
        Face Capture
      </h4>

      <div className="grid gap-4">
        <div className="relative aspect-video bg-black rounded-lg overflow-hidden">
          <img
            ref={previewRef}
            alt="Camera preview"
            className={`w-full h-full object-cover ${capturing ? "" : "hidden"}`}
          />
          {!capturing && (
            <div className="absolute inset-0 flex items-center justify-center text-muted-foreground">
              <Camera className="w-12 h-12 opacity-50" />
            </div>
          )}
        </div>

        <div className="flex gap-2">
          {!capturing ? (
            <Button type="button" onClick={startCamera} className="flex-1">
              <Camera className="w-4 h-4 mr-2" />
              Start Camera
            </Button>
          ) : (
            <>
              <Button type="button" onClick={capturePhoto} className="flex-1">
                <Circle className="w-4 h-4 mr-2" />
                Capture
              </Button>
              <Button type="button" variant="outline" onClick={stopCamera}>
                <Square className="w-4 h-4 mr-2" />
                Stop
              </Button>
            </>
          )}
        </div>

        {capturing && (
          <p className="text-[10px] text-muted-foreground">
            Native preview via openpnp-capture (works cross-distro).
          </p>
        )}

        {faceImages.length > 0 && (
          <div>
            <p className="text-sm text-muted-foreground mb-2">
              Saved Faces ({faceImages.length})
            </p>
            <div className="grid grid-cols-4 gap-2">
              {faceImages.map((path) => (
                <div key={path} className="relative group">
                  <div className="aspect-square bg-muted rounded-lg overflow-hidden">
                    <img
                      src={convertFileSrc(path)}
                      alt="Captured face"
                      className="w-full h-full object-cover"
                    />
                  </div>
                  <button
                    type="button"
                    onClick={() => deleteFace(path)}
                    className="absolute top-1 right-1 p-1 rounded bg-destructive/80 text-destructive-foreground cursor-pointer"
                  >
                    <Trash2 className="w-3 h-3 text-white" />
                  </button>
                </div>
              ))}
            </div>
          </div>
        )}
      </div>
    </div>
  );
}
