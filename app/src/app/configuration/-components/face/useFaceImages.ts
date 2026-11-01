import { useCallback, useEffect, useState } from "react";
import { toast } from "sonner";
import { cmd } from "@/commands";

/**
 * The saved face images, loaded on mount. `capture` saves a frame from the
 * running preview session; both actions reload the list and toast the result.
 */
export function useFaceImages() {
  const [faceImages, setFaceImages] = useState<string[]>([]);

  // Stable identity: the effect below depends on it, and a new function per
  // render would reload (and re-render) forever.
  const loadFaceImages = useCallback(async () => {
    try {
      const images = await cmd.face.listImages();
      setFaceImages(images);
    } catch (err) {
      console.error("Failed to load face images:", err);
    }
  }, []);

  useEffect(() => {
    loadFaceImages();
  }, [loadFaceImages]);

  async function capture() {
    try {
      await cmd.face.captureInSession();
      toast.success("Face image saved!");
      await loadFaceImages();
    } catch (err) {
      toast.error(`${err}`);
    }
  }

  async function remove(path: string) {
    try {
      await cmd.face.deleteImage(path);
      toast.success("Face image deleted");
      await loadFaceImages();
    } catch (err) {
      toast.error(`Failed to delete: ${err}`);
    }
  }

  return { faceImages, capture, remove };
}
