import { useMemo } from "react";
import { NumberField } from "../NumberField";
import { AntiSpoofingSection } from "./AntiSpoofingSection";
import { CameraDeviceSection } from "./CameraDeviceSection";
import { FaceCapture } from "./FaceCapture";
import { FaceModelsSection } from "./FaceModelsSection";
import { useModels } from "./useModels";
import { useVideoDevices } from "./useVideoDevices";

export function FaceSetting() {
  const models = useModels();
  const videoDevices = useVideoDevices();

  // Memoized so useModelFileStatus (in AntiSpoofingSection and ModelSelect)
  // rechecks files only when the model list changes, not on every render.
  const modelsByTask = useMemo(
    () => ({
      detection: models.filter((m) => m.task === "detection"),
      recognition: models.filter((m) => m.task === "recognition"),
      anti_spoofing: models.filter((m) => m.task === "anti_spoofing"),
    }),
    [models],
  );

  return (
    <div className="grid gap-4">
      <div className="grid grid-cols-2 gap-6 p-4 rounded-lg bg-muted/50 border border-border/50">
        <NumberField
          name="methods.face.retries"
          id="face-max-retries"
          label="Max Retries"
          min={1}
          max={10}
        />
        <NumberField
          name="methods.face.retry_delay"
          id="face-retry-delay"
          label="Retry Delay (ms)"
          min={0}
          max={5000}
        />
      </div>

      <CameraDeviceSection videoDevices={videoDevices} />

      <FaceCapture />

      <FaceModelsSection
        detectionModels={modelsByTask.detection}
        recognitionModels={modelsByTask.recognition}
      />

      <AntiSpoofingSection
        antiSpoofModels={modelsByTask.anti_spoofing}
        videoDevices={videoDevices}
      />
    </div>
  );
}
